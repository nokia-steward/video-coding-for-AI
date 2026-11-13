// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "global_data_instance_runtime.h"

void output_display_branch_runtime::reset_for_processing_cycle() {
    {
        std::lock_guard lock(bundle_reorder.mutex);
        bundle_reorder.pending.clear();
    }
    overlay_last_frame_num = G_MAXUINT64;
    bitrate_throttle = bitrate_display_throttle_state{};
    {
        std::lock_guard lock(border_dump_mutex);
        if (border_dump_stream.is_open()) {
            border_dump_stream.close();
        }
        border_dump_initialized = false;
        border_dump_failed = false;
    }
    display_roi_correlation_next_sequence.store(1U);
    display_roi_pts_correlation_debug_logs_remaining.store(5);
    display_roi_strict_fifo_miss_debug_logs_remaining.store(5);
    warned_strict_order_roi_correlation.store(false);
    warned_missing_output_mapping.store(false);
    warned_bitrate_missing_batch_meta = false;
    warned_bitrate_create_meta_failed = false;
    logged_bitrate_probe = false;
    warned_fps_missing_batch_meta = false;
    warned_fps_create_meta_failed = false;
    logged_fps_probe = false;
}

void output_debug_branch_runtime::reset_for_processing_cycle() {
    {
        std::lock_guard lock(bundle_reorder.mutex);
        bundle_reorder.pending.clear();
    }
    debug_roi_correlation_next_sequence.store(1U);
    debug_roi_pts_correlation_debug_logs_remaining.store(5);
    debug_roi_strict_fifo_miss_debug_logs_remaining.store(5);
    warned_strict_order_roi_correlation.store(false);
    warned_missing_output_mapping.store(false);
}

void output_pipeline_probe_runtime::reset_for_processing_cycle() {
    random_access_timestamp_fix_frame_count = 0;
    random_access_timestamp_base = GST_CLOCK_TIME_NONE;
    display_decoder_input_log_frame_count = 0;
    tee_display_queue_sink_log_frame_count = 0;
}

void udp_input_runtime_state::reset_connection_packet_stats() {
    first_udp_packet_received.store(false);
    udp_packet_count.store(0);
    udp_bytes_received.store(0);
    udp_min_packet_size.store(0);
    udp_max_packet_size.store(0);
    udp_total_packet_sizes.store(0);
    last_packet_time_rep.store(0);
}

void udp_input_runtime_state::reset_frame_tracking_counters() {
    frames_after_parser.store(0);
    frames_into_queue_before_decoder.store(0);
    frames_after_queue_before_decoder.store(0);
    frames_into_decoder.store(0);
    frames_after_decoder.store(0);
    parser_full_frame_probe_call_count.store(0);
}
