// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "bridge/context/metadata_frame_bundle.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <gst/gst.h>
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @file global_data_instance_runtime.h
 * @brief Per-instance mutable runtime previously held in file-static or process-global state (M9).
 */

/**
 * @brief Reorders metadata_frame_bundle consumption when queues arrive out of order.
 */
struct output_metadata_bundle_reorder_buffer {
    std::mutex mutex{};
    std::unordered_map<uint64_t, metadata_frame_bundle> pending{};
};

/**
 * @brief Throttles bitrate overlay redraws on the display branch.
 */
struct bitrate_display_throttle_state {
    GstClockTime last_update_time = GST_CLOCK_TIME_NONE;
    std::string last_bitrate_str;
    static constexpr GstClockTime update_interval_ns = GST_SECOND;
};

/**
 * @brief Per-instance display branch probe and border-dump state.
 */
struct output_display_branch_runtime {
    output_metadata_bundle_reorder_buffer bundle_reorder{};
    guint64 overlay_last_frame_num = G_MAXUINT64;
    bitrate_display_throttle_state bitrate_throttle{};
    std::mutex border_dump_mutex{};
    std::ofstream border_dump_stream{};
    bool border_dump_initialized = false;
    bool border_dump_failed = false;
    /// Next expected contract sequence when decoded-buffer PTS does not match `register_output_sequence` keys.
    std::atomic_uint64_t display_roi_correlation_next_sequence{1};
    /// Logs a few PTS-map misses at debug level to confirm encoded vs decoded timestamp domains differ.
    std::atomic_int display_roi_pts_correlation_debug_logs_remaining{5};
    /// Throttles debug logs when strict-order border path has no FIFO bundle yet (no fail-stop).
    std::atomic_int display_roi_strict_fifo_miss_debug_logs_remaining{5};
    std::atomic_bool warned_strict_order_roi_correlation{false};
    std::atomic_bool warned_missing_output_mapping{false};
    bool warned_bitrate_missing_batch_meta = false;
    bool warned_bitrate_create_meta_failed = false;
    bool logged_bitrate_probe = false;
    bool warned_fps_missing_batch_meta = false;
    bool warned_fps_create_meta_failed = false;
    bool logged_fps_probe = false;

    void reset_for_processing_cycle();
};

/**
 * @brief Per-instance debug branch probe state.
 */
struct output_debug_branch_runtime {
    output_metadata_bundle_reorder_buffer bundle_reorder{};
    std::atomic_uint64_t debug_roi_correlation_next_sequence{1};
    std::atomic_int debug_roi_pts_correlation_debug_logs_remaining{5};
    std::atomic_int debug_roi_strict_fifo_miss_debug_logs_remaining{5};
    std::atomic_bool warned_strict_order_roi_correlation{false};
    std::atomic_bool warned_missing_output_mapping{false};

    void reset_for_processing_cycle();
};

/**
 * @brief Per-instance counters for output-pipeline diagnostic pad probes (appspace multi-instance safety).
 *
 * Replaces file-static state in output queue/decoder probes so reconnect and multiple encoder_cli
 * instances do not share frame indices or random_access timestamp repair baselines.
 */
struct output_pipeline_probe_runtime {
    int random_access_timestamp_fix_frame_count = 0;
    GstClockTime random_access_timestamp_base = GST_CLOCK_TIME_NONE;
    int display_decoder_input_log_frame_count = 0;
    int tee_display_queue_sink_log_frame_count = 0;

    void reset_for_processing_cycle();
};

/**
 * @brief UDP packet and frame-probe counters scoped to one global_data instance.
 */
struct udp_input_runtime_state {
    std::atomic_bool first_udp_packet_received{false};
    std::atomic<guint64> udp_packet_count{0};
    std::atomic<guint64> udp_bytes_received{0};
    std::atomic<std::chrono::steady_clock::time_point::rep> last_packet_time_rep{0};
    std::atomic<guint64> udp_min_packet_size{0};
    std::atomic<guint64> udp_max_packet_size{0};
    std::atomic<guint64> udp_total_packet_sizes{0};

    std::atomic<guint64> frames_after_parser{0};
    std::atomic<guint64> frames_into_queue_before_decoder{0};
    std::atomic<guint64> frames_after_queue_before_decoder{0};
    std::atomic<guint64> frames_into_decoder{0};
    std::atomic<guint64> frames_after_decoder{0};
    std::atomic<guint64> parser_full_frame_probe_call_count{0};

    void reset_connection_packet_stats();
    void reset_frame_tracking_counters();
};
