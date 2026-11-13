// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../pipeline.h"
#include "callbacks.h"
#include "creation.h"
#include "linking.h"
#include "logging.h"
#include "probes.h"
#include "shutdown.h"
#include "statistics.h"
#include "utils.h"

#include <chrono>
#include <gst/gst.h>
#include <mutex>

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

namespace {
    /**
     * @brief Acquires stop ownership or waits for active stop completion.
     *
     * @return true when caller owns the stop sequence and must perform shutdown;
     *         false when shutdown already completed by another caller.
     */
    [[nodiscard]] bool begin_pipeline_stop(rtp_pipeline &pipeline, const char *stop_kind) {
        if (pipeline.runtime_context.pipeline_stopped.load()) {
            sf::debug(pipeline_category, "%s for port %u skipped: pipeline already stopped", stop_kind, pipeline.port);
            return false;
        }

        bool expected = false;
        if (pipeline.runtime_context.pipeline_stop_in_progress.compare_exchange_strong(expected, true)) {
            return true;
        }

        sf::info(pipeline_category,
                 "%s requested for port %u while another stop is in progress; waiting for completion", stop_kind,
                 pipeline.port);
        std::unique_lock stop_wait_lock(pipeline.runtime_context.state_mutex);
        pipeline.runtime_context.state_cv.wait(stop_wait_lock, [&pipeline] {
            return !pipeline.runtime_context.pipeline_stop_in_progress.load() ||
                   pipeline.runtime_context.pipeline_stopped.load();
        });

        if (pipeline.runtime_context.pipeline_stopped.load()) {
            sf::debug(pipeline_category, "%s for port %u skipped: stop completed by another caller", stop_kind,
                      pipeline.port);
            return false;
        }

        // The previous stop attempt exited without completing the full stop sequence.
        expected = false;
        if (pipeline.runtime_context.pipeline_stop_in_progress.compare_exchange_strong(expected, true)) {
            sf::warning(pipeline_category, "%s for port %u taking over after incomplete prior stop attempt", stop_kind,
                        pipeline.port);
            return true;
        }

        sf::warning(pipeline_category, "%s for port %u could not acquire stop ownership after wait; skipping",
                    stop_kind, pipeline.port);
        return false;
    }

    /**
     * @brief Releases stop-in-progress flag on scope exit.
     */
    struct stop_in_progress_guard {
        pipeline_runtime_context &runtime_context_ref;

        explicit stop_in_progress_guard(pipeline_runtime_context &runtime_context) :
            runtime_context_ref(runtime_context) {}

        ~stop_in_progress_guard() {
            runtime_context_ref.pipeline_stop_in_progress.store(false);
            runtime_context_ref.notify_state_change();
        }
    };
} // namespace

rtp_pipeline::rtp_pipeline(const guint16 port_param, GMainLoop *loop, const rtp_output_config &rtp_output_config_param,
                           const std::optional<std::string> &output_directory_param,
                           const sf::background_color background_color_param,
                           const transport_context transport_ctx_param, const bool draw_bitrate_param,
                           const bool draw_bitrate_large_param, const bool draw_fps_param,
                           const rtp_server_config *server_config_param) :
    client_session_ptr(nullptr),
    // Transport-specific elements (public)
    rtp_input_source(nullptr), webrtcbin(nullptr), rtp_input_capsfilter(nullptr), webrtc_rtp_capsfilter(nullptr),
    // Shared input elements (public)
    rtph265depay(nullptr), h265parse(nullptr), h265_capsfilter(nullptr), queue_encoded(nullptr),
    // Output elements (public - must be initialized before private members)
    decoded_output_capsfilter(nullptr), gpu_video_convert(nullptr), tee(nullptr), queue_display(nullptr),
    display_convert(nullptr), capsfilter_display(nullptr), nvosd_display(nullptr), post_osd_tee(nullptr),
    output_sink(nullptr), queue_file(nullptr), h265_encoder(nullptr), filesink(nullptr), queue_rtp_output(nullptr),
    h265_encoder_rtp_output(nullptr), rtph265pay_rtp_output(nullptr), rtp_output_sink(nullptr),
    queue_debug_frames(nullptr), debug_frames_download(nullptr), debug_frames_convert(nullptr),
    debug_frames_png_encoder(nullptr), debug_frames_sink(nullptr),
    // DeepStream elements (for display branch inference)
    nvstreammux(nullptr), nvinfer(nullptr), nvtracker(nullptr),
    // Class IDs for label filtering
    class_ids_to_filter(),
    // Test mode context
    test_mode_ctx(nullptr),
    // Configuration (public - must be initialized before private members)
    port(port_param), main_loop(loop), filename(), rtp_output(rtp_output_config_param),
    output_directory(output_directory_param), server_config(server_config_param),
    // Single pipeline
    main_pipeline(nullptr),
    // Processing elements
    sf_h265_sei_extract(nullptr), h265_decoder(nullptr), decoded_converter(nullptr), decoded_capsfilter(nullptr),
    sf_pri_reconstruct(nullptr), sf_eoi_luma_backscale(nullptr), sf_tet_apply(nullptr), queue_decoded(nullptr),
    background_color(background_color_param), transport_ctx(transport_ctx_param),
    transport_type_enum(transport_ctx_param.functions.get_transport_type
                                ? transport_ctx_param.functions.get_transport_type()
                                : transport_type::rtp),
    draw_bitrate(draw_bitrate_param), draw_bitrate_large(draw_bitrate_large_param), draw_fps(draw_fps_param),
    app_adapter(*this), output_controller(), runtime_context(), stats_context(),
    transport_build_ctx(port, transport_ctx, transport_type_enum, server_config, runtime_context, rtp_input_source,
                        webrtcbin, rtp_input_capsfilter, webrtc_rtp_capsfilter, rtph265depay, h265parse,
                        h265_capsfilter, queue_encoded) {}

rtp_pipeline::~rtp_pipeline() {
    sf::info(pipeline_category, "Pipeline destructor called for port %u", port);
    if (!runtime_context.pipeline_stopped.load()) {
        stop_pipeline();
    }
    ::cleanup_elements(*this);
}

bool rtp_pipeline::create_pipeline() {
    sf::info(pipeline_category, "Creating main GStreamer pipeline for port %u...", port);

    main_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("rtp-main-pipeline"));
    if (!main_pipeline) {
        sf::error(pipeline_category, "Failed to create main pipeline for port %u", port);
        cleanup_elements(*this);
        return false;
    }

    if (!::create_elements(*this)) {
        sf::error(pipeline_category, "Failed to create elements for port %u", port);
        ::cleanup_elements(*this);
        return false;
    }

    if (!::configure_elements(*this)) {
        sf::error(pipeline_category, "Failed to configure elements for port %u", port);
        ::cleanup_elements(*this);
        return false;
    }

    if (!::add_elements_to_pipelines(*this)) {
        sf::error(pipeline_category, "Failed to add elements to pipeline for port %u", port);
        ::cleanup_elements(*this);
        return false;
    }

    if (!::link_input_pipeline(*this)) {
        sf::error(pipeline_category, "Failed to link transport input chain for port %u", port);
        ::cleanup_elements(*this);
        return false;
    }

    if (!::link_decode_and_output_pipeline(*this)) {
        sf::error(pipeline_category, "Failed to link decode/output chain for port %u", port);
        ::cleanup_elements(*this);
        return false;
    }

    setup_bus_watchers();
    ::add_pad_probes(*this);

    sf::info(pipeline_category, "Main pipeline for port %u created successfully", port);
    return true;
}

bool rtp_pipeline::start_pipeline() {
    if (!main_pipeline) {
        sf::error(pipeline_category, "Pipeline not created for port %u", port);
        return false;
    }

    stats_context.inference_frames_observed = 0;
    runtime_context.pipeline_stop_in_progress = false;
    runtime_context.pipeline_stopped = false;
    runtime_context.notify_state_change();

    if (const GstStateChangeReturn ret = gst_element_set_state(main_pipeline.get(), GST_STATE_PLAYING);
        ret == GST_STATE_CHANGE_FAILURE) {
        sf::error(pipeline_category, "Failed to set main pipeline to PLAYING state for port %u", port);
        return false;
    }

    sf::info(pipeline_category, "Main pipeline for port %u is running.", port);
    return true;
}

void rtp_pipeline::stop_pipeline() {
    if (!begin_pipeline_stop(*this, "stop_pipeline")) {
        return;
    }
    stop_in_progress_guard stop_guard(runtime_context);

    sf::info(pipeline_category, "Stopping pipeline for port %u", port);

    disconnect_signal_handlers(*this);
    stop_all_pipelines(*this);
    log_pipeline_statistics();

    runtime_context.pipeline_stopped = true;
    runtime_context.notify_state_change();
    sf::info(pipeline_category, "Pipeline stopped for port %u", port);
}

void rtp_pipeline::stop_pipeline_graceful() {
    if (!begin_pipeline_stop(*this, "stop_pipeline_graceful")) {
        return;
    }
    stop_in_progress_guard stop_guard(runtime_context);

    sf::info(pipeline_category, "Stopping pipeline gracefully for port %u", port);

    if (main_pipeline) {
        sf::info(pipeline_category, "Sending EOS to main pipeline for port %u", port);
        gst_element_send_event(main_pipeline.get(), gst_event_new_eos());
        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(main_pipeline.get()));
        if (bus) {
            (void) gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND,
                                              static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            gst_object_unref(bus);
        }
    }

    if (test_mode_ctx && nvinfer) {
        const guint64 expected_frames = stats_context.statistics.display_frames_sent.load();
        const auto wait_start = std::chrono::steady_clock::now();
        constexpr auto wait_timeout = std::chrono::seconds(45);
        std::unique_lock inference_wait_lock(runtime_context.state_mutex);
        while (stats_context.inference_frames_observed.load() < expected_frames &&
               (std::chrono::steady_clock::now() - wait_start) < wait_timeout) {
            const auto remaining = wait_timeout - (std::chrono::steady_clock::now() - wait_start);
            runtime_context.state_cv.wait_for(inference_wait_lock, remaining, [this, expected_frames] {
                return stats_context.inference_frames_observed.load() >= expected_frames;
            });
        }

        const guint64 observed_frames = stats_context.inference_frames_observed.load();
        if (observed_frames < expected_frames) {
            sf::warning(pipeline_category,
                        "Graceful stop timeout waiting for inference drain on port %u: observed=%lu expected=%lu", port,
                        observed_frames, expected_frames);
        } else {
            sf::info(pipeline_category, "Inference drain complete for port %u: observed=%lu expected=%lu", port,
                     observed_frames, expected_frames);
        }
    }

    disconnect_signal_handlers(*this);
    stop_all_pipelines(*this);
    log_pipeline_statistics();

    runtime_context.pipeline_stopped = true;
    runtime_context.notify_state_change();
    sf::info(pipeline_category, "Pipeline stopped for port %u", port);
}

void rtp_pipeline::setup_bus_watchers() const {
    sf::info(pipeline_category, "Setting up bus watcher for port %u", port);

    if (main_pipeline) {
        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(main_pipeline.get()));
        gst_bus_add_watch(bus, on_pipeline_message, &const_cast<rtp_pipeline *>(this)->transport_build_ctx);
        gst_object_unref(bus);
    }

    sf::info(pipeline_category, "Bus watcher setup completed for port %u", port);
}

void rtp_pipeline::log_pipeline_statistics() const { ::log_pipeline_statistics(*this); }

void rtp_pipeline::log_periodic_statistics() const { ::log_periodic_statistics(*this); }

void rtp_pipeline::set_client_session(client_session *session) {
    client_session_ptr = session;
    sf::info(pipeline_category, "Client session set for port %u", port);
}
