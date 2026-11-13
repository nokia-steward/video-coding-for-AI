#include "rtp_output_branch.h"
#include "../../pipeline.h"
#include "../branch_utils.h"
#include "gst/gpu_elements.h"
#include "logging.h"

#include <gst/gst.h>

// Debug category for RTP output branch operations.
GST_DEBUG_CATEGORY_STATIC(rtp_output_branch_category);
#define GST_CAT_DEFAULT rtp_output_branch_category

namespace {
    [[nodiscard]] optional_branch_status resolve_rtp_output_branch_status(const rtp_pipeline &pipeline) {
        return pipeline.rtp_output.enabled ? optional_branch_status::enabled : optional_branch_status::disabled;
    }

    [[nodiscard]] optional_branch_gate_decision gate_rtp_output_branch(const rtp_pipeline *pipeline) {
        return get_optional_branch_gate_decision(pipeline, resolve_rtp_output_branch_status);
    }
} // namespace

output_branch_functions create_rtp_output_branch_functions() {
    return {.is_enabled = [](const rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_rtp_output_branch(pipeline);
                return gate_decision.is_valid_pipeline && gate_decision.should_execute;
            },

            .create_elements = [](rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_rtp_output_branch(pipeline);
                if (!gate_decision.is_valid_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true; // Not an error if disabled
                }

                pipeline->queue_rtp_output =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
                try {
                    // RAII_EXEMPT: GstBin takes ownership of this raw GstElement pointer.
                    pipeline->h265_encoder_rtp_output = sf::unique_gst_element_no_delete_ptr(
                            create_h265_encoder("rtp-output-encoder", 0).release());
                } catch (const std::exception &error) {
                    sf::error(rtp_output_branch_category, "Failed to create GPU encoder for port %u: %s",
                              pipeline->get_port(), error.what());
                    return false;
                }
                pipeline->rtph265pay_rtp_output =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("rtph265pay", nullptr));
                pipeline->rtp_output_sink =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("udpsink", "rtp-output-sink"));

                if (!has_all_required_elements(
                            {pipeline->queue_rtp_output.get(), pipeline->h265_encoder_rtp_output.get(),
                             pipeline->rtph265pay_rtp_output.get(), pipeline->rtp_output_sink.get()})) {
                    sf::error(rtp_output_branch_category, "Failed to create RTP output elements for port %u",
                              pipeline->get_port());
                    return false;
                }

                sf::info(rtp_output_branch_category,
                         "RTP output elements created for port %u: host=%s, port=%u, payload-type=%u",
                         pipeline->get_port(), pipeline->rtp_output.host.c_str(), pipeline->rtp_output.port,
                         pipeline->rtp_output.payload_type);
                return true;
            },

            .configure_elements = [](rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_rtp_output_branch(pipeline);
                if (!gate_decision.is_valid_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                if (!has_all_required_elements(
                            {pipeline->queue_rtp_output.get(), pipeline->h265_encoder_rtp_output.get(),
                             pipeline->rtph265pay_rtp_output.get(), pipeline->rtp_output_sink.get()})) {
                    sf::error(rtp_output_branch_category,
                              "RTP output configuration requested but branch elements are missing for port %u",
                              pipeline->get_port());
                    return false;
                }

                // Configure queue
                configure_queue_element(pipeline->queue_rtp_output.get(), static_cast<guint64>(50 * 1024 * 1024),
                                        static_cast<guint>(100), static_cast<guint64>(2 * GST_SECOND));

                const guint payload_type = pipeline->rtp_output.payload_type;
                // Configure RTP payloader for H.265 over RTP.
                // config-interval=-1 re-sends VPS/SPS/PPS on each IDR for robust late-join playback.
                g_object_set(G_OBJECT(pipeline->rtph265pay_rtp_output.get()), "pt", payload_type, "config-interval", -1,
                             nullptr);

                // Configure output sink
                g_object_set(G_OBJECT(pipeline->rtp_output_sink.get()), "host", pipeline->rtp_output.host.c_str(),
                             "port", static_cast<gint>(pipeline->rtp_output.port), "async", FALSE, "sync", FALSE,
                             nullptr);
                sf::info(rtp_output_branch_category,
                         "RTP output enabled - Forwarding H.265 RTP stream to %s:%u for port %u (PT=%u)",
                         pipeline->rtp_output.host.c_str(), pipeline->rtp_output.port, pipeline->get_port(),
                         payload_type);

                return true;
            },

            .link_branch = [](rtp_pipeline *pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_rtp_output_branch(pipeline);
                if (!gate_decision.is_valid_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                if (!has_all_required_elements(
                            {pipeline->queue_rtp_output.get(), pipeline->h265_encoder_rtp_output.get(),
                             pipeline->rtph265pay_rtp_output.get(), pipeline->rtp_output_sink.get()})) {
                    sf::error(rtp_output_branch_category,
                              "RTP output branch linking requested but RTP output is not enabled or elements are "
                              "missing for port %u",
                              pipeline->get_port());
                    return false;
                }

                // Link: queue_rtp_output -> h265_encoder_rtp_output -> rtph265pay_rtp_output -> rtp_output_sink
                return gst_element_link_many(pipeline->queue_rtp_output.get(), pipeline->h265_encoder_rtp_output.get(),
                                             pipeline->rtph265pay_rtp_output.get(), pipeline->rtp_output_sink.get(),
                                             nullptr);
            },

            .link_to_tee = [](rtp_pipeline *pipeline, GstElement *tee_element) -> bool {
                const optional_branch_gate_decision gate_decision = gate_rtp_output_branch(pipeline);
                if (!gate_decision.is_valid_pipeline || !tee_element) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                return link_tee_to_branch_queue(rtp_output_branch_category, "RTP output", pipeline->get_port(),
                                                tee_element, pipeline->queue_rtp_output.get());
            },

            .add_to_pipeline = [](rtp_pipeline *pipeline, GstElement *output_pipeline) -> bool {
                const optional_branch_gate_decision gate_decision = gate_rtp_output_branch(pipeline);
                if (!gate_decision.is_valid_pipeline || !output_pipeline) {
                    return false;
                }
                if (!gate_decision.should_execute) {
                    return true;
                }

                gst_bin_add_many(GST_BIN(output_pipeline), pipeline->queue_rtp_output.get(),
                                 pipeline->h265_encoder_rtp_output.get(), pipeline->rtph265pay_rtp_output.get(),
                                 pipeline->rtp_output_sink.get(), nullptr);
                return true;
            },

            .cleanup = [](rtp_pipeline *pipeline) -> void {
                (void) pipeline;
                // No special cleanup needed - elements are managed by pipeline
            },

            .get_branch_name = []() -> const char * { return "rtp-output"; }};
}

output_branch_context create_rtp_output_branch_context() { return {.functions = create_rtp_output_branch_functions()}; }
