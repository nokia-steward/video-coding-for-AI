// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <glib.h>
#include <gst/gst.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../../core/rtp_config.h"
#include "../../core/rtp_server_config.h"
#include "../../transport/transport_interface.h"
#include "bitrate_calculation.h"
#include "gst_raii.h"
#include "pipeline_fps_monitor.h"

struct client_session;
struct transport_build_context;
struct pipeline_app_adapter;

/**
 * @brief WebRTC signaling state owned by runtime context.
 */
struct webrtc_signal_data {
    transport_build_context *build_context = nullptr;
    sf::communication::signaling_server *signaling_server = nullptr;
    std::string client_id;
};

/**
 * @brief Aggregated data-flow and media statistics for one pipeline.
 */
struct pipeline_statistics {
    guint64 total_bytes = 0;
    guint num_frames = 0;
    gint width = 0;
    gint height = 0;
    gint framerate_numerator = 0;
    gint framerate_denominator = 0;
    const gchar *format = nullptr;

    std::atomic<guint64> rtp_input_packets_received{0};
    std::atomic<guint64> rtp_input_bytes_received{0};
    std::atomic<guint64> rtp_packets_depayloaded{0};
    std::atomic<guint64> h265_frames_parsed{0};
    std::atomic<guint64> h265_bytes_parsed{0};
    std::atomic<guint64> appsink_samples_received{0};
    std::atomic<guint64> appsink_bytes_received{0};
    std::atomic<guint64> appsrc_samples_pushed{0};
    std::atomic<guint64> appsrc_bytes_pushed{0};
    std::atomic<guint64> decoded_frames_received{0};
    std::atomic<guint64> decoded_bytes_received{0};
    std::atomic<guint64> decoded_appsrc_samples_pushed{0};
    std::atomic<guint64> decoded_appsrc_bytes_pushed{0};
    std::atomic<guint64> display_frames_sent{0};
    std::atomic<guint64> file_bytes_written{0};
    std::atomic<guint64> rtp_output_packets_sent{0};
    std::atomic<guint64> rtp_output_bytes_sent{0};
    std::atomic<guint64> gpu_to_cpu_sample_conversions{0};
    std::atomic<guint64> writable_buffer_conversions{0};
    std::atomic<guint64> cpu_mutation_stage_failures{0};

    mutable sf::bitrate_tracker incoming_bitrate_tracker;

    pipeline_statistics() :
        total_bytes(0), num_frames(0), width(0), height(0), framerate_numerator(0), framerate_denominator(0),
        format(nullptr), rtp_input_packets_received(0), rtp_input_bytes_received(0), rtp_packets_depayloaded(0),
        h265_frames_parsed(0), h265_bytes_parsed(0), appsink_samples_received(0), appsink_bytes_received(0),
        appsrc_samples_pushed(0), appsrc_bytes_pushed(0), decoded_frames_received(0), decoded_bytes_received(0),
        decoded_appsrc_samples_pushed(0), decoded_appsrc_bytes_pushed(0), display_frames_sent(0), file_bytes_written(0),
        rtp_output_packets_sent(0), rtp_output_bytes_sent(0), gpu_to_cpu_sample_conversions(0),
        writable_buffer_conversions(0), cpu_mutation_stage_failures(0), incoming_bitrate_tracker() {}
    pipeline_statistics(const pipeline_statistics &) = delete;
    pipeline_statistics &operator=(const pipeline_statistics &) = delete;
    pipeline_statistics(pipeline_statistics &&) = delete;
    pipeline_statistics &operator=(pipeline_statistics &&) = delete;
};

/**
 * @brief Per-pipeline mutable display overlay state.
 */
struct display_overlay_state {
    guint64 last_frame_num = UINT64_MAX;
    GstClockTime bitrate_last_update_time = GST_CLOCK_TIME_NONE;
    std::string last_bitrate_string;

    bool display_sink_first_buffer_logged = false;
    bool nvosd_src_first_buffer_logged = false;
    bool bitrate_warned_missing_meta = false;
    bool bitrate_warned_create_meta = false;
    bool bitrate_probe_logged = false;
    bool fps_warned_missing_meta = false;
    bool fps_warned_create_meta = false;
    bool fps_probe_logged = false;

    display_overlay_state() :
        last_frame_num(UINT64_MAX), bitrate_last_update_time(GST_CLOCK_TIME_NONE), last_bitrate_string(),
        display_sink_first_buffer_logged(false), nvosd_src_first_buffer_logged(false),
        bitrate_warned_missing_meta(false), bitrate_warned_create_meta(false), bitrate_probe_logged(false),
        fps_warned_missing_meta(false), fps_warned_create_meta(false), fps_probe_logged(false) {}
    display_overlay_state(const display_overlay_state &) = delete;
    display_overlay_state &operator=(const display_overlay_state &) = delete;
    display_overlay_state(display_overlay_state &&) = delete;
    display_overlay_state &operator=(display_overlay_state &&) = delete;
};

/**
 * @brief Holds GStreamer signal handler metadata for later disconnection.
 */
struct pipeline_signal_handler_info {
    GstElement *element = nullptr;
    gulong handler_id = 0;
};

/**
 * @brief Runtime state for lifecycle coordination.
 */
struct pipeline_runtime_context {
    std::vector<pipeline_signal_handler_info> signal_handler_infos;
    std::unique_ptr<webrtc_signal_data> webrtc_signal_data_ptr;
    std::atomic_bool pipeline_stop_in_progress{false};
    std::atomic_bool pipeline_stopped{false};
    std::mutex state_mutex;
    std::condition_variable state_cv;

    pipeline_runtime_context() :
        signal_handler_infos(), webrtc_signal_data_ptr(nullptr), pipeline_stop_in_progress(false),
        pipeline_stopped(false), state_mutex(), state_cv() {}

    void notify_state_change() { state_cv.notify_all(); }

    pipeline_runtime_context(const pipeline_runtime_context &) = delete;
    pipeline_runtime_context &operator=(const pipeline_runtime_context &) = delete;
    pipeline_runtime_context(pipeline_runtime_context &&) = delete;
    pipeline_runtime_context &operator=(pipeline_runtime_context &&) = delete;
};

/**
 * @brief Statistics and overlay state context.
 */
struct pipeline_stats_context {
    pipeline_statistics statistics;
    display_overlay_state overlay_state;
    sf::pipeline_fps_monitor fps_monitor;
    std::atomic<guint64> inference_frames_observed{0};

    pipeline_stats_context() : statistics(), overlay_state(), fps_monitor(), inference_frames_observed(0) {}
};

/**
 * @brief Context for transport/input element build and linkage.
 */
struct transport_build_context {
    guint16 &port;
    transport_context &transport_ctx;
    transport_type &transport_type_enum;
    const rtp_server_config *&server_config;
    pipeline_runtime_context &runtime_context;

    sf::unique_gst_element_no_delete_ptr &rtp_input_source;
    sf::unique_gst_element_no_delete_ptr &webrtcbin;
    sf::unique_gst_element_no_delete_ptr &rtp_input_capsfilter;
    sf::unique_gst_element_no_delete_ptr &webrtc_rtp_capsfilter;
    sf::unique_gst_element_no_delete_ptr &rtph265depay;
    sf::unique_gst_element_no_delete_ptr &h265parse;
    sf::unique_gst_element_no_delete_ptr &h265_capsfilter;
    sf::unique_gst_element_no_delete_ptr &queue_encoded;

    transport_build_context(guint16 &port_ref, transport_context &transport_ctx_ref,
                            transport_type &transport_type_enum_ref, const rtp_server_config *&server_config_ref,
                            pipeline_runtime_context &runtime_context_ref,
                            sf::unique_gst_element_no_delete_ptr &rtp_input_source_ref,
                            sf::unique_gst_element_no_delete_ptr &webrtcbin_ref,
                            sf::unique_gst_element_no_delete_ptr &rtp_input_capsfilter_ref,
                            sf::unique_gst_element_no_delete_ptr &webrtc_rtp_capsfilter_ref,
                            sf::unique_gst_element_no_delete_ptr &rtph265depay_ref,
                            sf::unique_gst_element_no_delete_ptr &h265parse_ref,
                            sf::unique_gst_element_no_delete_ptr &h265_capsfilter_ref,
                            sf::unique_gst_element_no_delete_ptr &queue_encoded_ref) :
        port(port_ref),
        transport_ctx(transport_ctx_ref), transport_type_enum(transport_type_enum_ref),
        server_config(server_config_ref), runtime_context(runtime_context_ref), rtp_input_source(rtp_input_source_ref),
        webrtcbin(webrtcbin_ref), rtp_input_capsfilter(rtp_input_capsfilter_ref),
        webrtc_rtp_capsfilter(webrtc_rtp_capsfilter_ref), rtph265depay(rtph265depay_ref), h265parse(h265parse_ref),
        h265_capsfilter(h265_capsfilter_ref), queue_encoded(queue_encoded_ref) {}
};
