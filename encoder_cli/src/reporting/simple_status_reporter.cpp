// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "simple_status_reporter.h"
#include "../../libvcai/include/communication/transport.h"
#include "../encoder_enums.h"
#include "../gst/input_types/frame_drop_manager.h"

#include <algorithm>
#include <gst/gst.h>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include "../../libvcai/include/logging.h"

/**
 * @file simple_status_reporter.cpp
 * @brief Implementation of the simplified status reporting system.
 */

/// GStreamer debug category for simple status reporting.
GST_DEBUG_CATEGORY_STATIC(simple_status_reporter_category);

void init_simple_status_reporter_debug_category() {
    GST_DEBUG_CATEGORY_INIT(simple_status_reporter_category, "simple-status-reporter", 0,
                            "Simplified status reporting and progress tracking");
}

simple_status_reporter::simple_status_reporter(
        const encoder_config &params_ref, bypass_state &bypass_ref, memory_policy_runtime &memory_runtime_ref,
        metadata_contract_runtime &metadata_runtime_ref, sf::pipeline_fps_monitor &fps_monitor_ref,
        std::atomic_bool &stop_processing_ref, udp_input_runtime_state &udp_input_runtime_ref,
        frame_drop_manager &udp_frame_drop_manager_ref, std::atomic<double> &luma_downscale_ref,
        std::atomic<double> &luma_upscale_ref) :
    params(params_ref),
    bypass(bypass_ref), memory_runtime(memory_runtime_ref), metadata_runtime(metadata_runtime_ref),
    fps_monitor(fps_monitor_ref), stop_processing(stop_processing_ref), udp_input_runtime(udp_input_runtime_ref),
    udp_frame_drop_manager(udp_frame_drop_manager_ref), luma_downscale(luma_downscale_ref),
    luma_upscale(luma_upscale_ref), start_time(std::chrono::steady_clock::now()) {
    sf::info(simple_status_reporter_category, "Simple status reporter initialized");
}

void simple_status_reporter::update_frame_processing() { frames_processed.fetch_add(1); }

void simple_status_reporter::update_encoding() { frames_encoded.fetch_add(1); }

void simple_status_reporter::update_pushed_to_encoder() { frames_pushed_to_encoder.fetch_add(1); }

void simple_status_reporter::update_output() { frames_output.fetch_add(1); }

void simple_status_reporter::update_output_processed() { frames_output_processed.fetch_add(1); }

void simple_status_reporter::update_output_display() { frames_output_display.fetch_add(1); }

void simple_status_reporter::update_output_h265() { frames_output_h265.fetch_add(1); }

void simple_status_reporter::update_output_mp4() { frames_output_mp4.fetch_add(1); }

void simple_status_reporter::update_output_debug() { frames_output_debug.fetch_add(1); }

void simple_status_reporter::update_output_rtp() { frames_output_rtp.fetch_add(1); }

void simple_status_reporter::update_udp_decoded() { udp_frames_decoded.fetch_add(1); }

void simple_status_reporter::update_roi_detection(const int objects_detected, const int objects_tracked) {
    roi_objects_detected.fetch_add(objects_detected);
    roi_objects_tracked.fetch_add(objects_tracked);
}

void simple_status_reporter::update_roi_filtering(const int objects_filtered) {
    roi_objects_filtered.fetch_add(objects_filtered);
}

void simple_status_reporter::update_sei_injection(const int sei_count) { sei_messages_injected.fetch_add(sei_count); }

void simple_status_reporter::report_error() { processing_errors.fetch_add(1); }

void simple_status_reporter::report_event(const std::string &event_description) const {
    output_status(sf::log_level::info, event_description);
}

void simple_status_reporter::report_pipeline_status(const std::string &pipeline_name, const std::string &status) const {
    const std::string message = "Pipeline '" + pipeline_name + "' status: " + status;
    output_status(sf::log_level::info, message);
}

std::string simple_status_reporter::build_status_report_string() const {
    const auto now = std::chrono::steady_clock::now();
    const auto total_runtime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);

    // Calculate rates
    const double total_fps = frames_processed.load() > 0 ? static_cast<double>(frames_processed.load()) /
                                                                   static_cast<double>(total_runtime.count())
                                                         : 0.0;

    const double encoding_fps = frames_encoded.load() > 0 ? static_cast<double>(frames_encoded.load()) /
                                                                    static_cast<double>(total_runtime.count())
                                                          : 0.0;

    const double output_fps = frames_output.load() > 0 ? static_cast<double>(frames_output.load()) /
                                                                 static_cast<double>(total_runtime.count())
                                                       : 0.0;

    // Build comprehensive status report
    std::ostringstream status_report;
    status_report << "=== ENCODER STATUS REPORT ===";
    status_report << "\nRuntime: " << total_runtime.count() << "s";

    // Build UDP statistics lines if in UDP mode
    std::vector<std::string> udp_lines;
    if (params.input_type_value == input_type::udp) {
        const guint64 udp_packets = udp_input_runtime.udp_packet_count.load();
        const guint64 udp_bytes = udp_input_runtime.udp_bytes_received.load();
        const double udp_packets_per_sec =
                udp_packets > 0 ? static_cast<double>(udp_packets) / static_cast<double>(total_runtime.count()) : 0.0;
        const double udp_bytes_per_sec =
                udp_bytes > 0 ? static_cast<double>(udp_bytes) / static_cast<double>(total_runtime.count()) : 0.0;
        const double udp_mbps = (udp_bytes_per_sec * 8.0) / (1024.0 * 1024.0); // Convert to Mbps
        const double avg_packet_size = udp_input_runtime.udp_packet_count.load() > 0
                                               ? static_cast<double>(udp_input_runtime.udp_total_packet_sizes.load()) /
                                                         static_cast<double>(udp_input_runtime.udp_packet_count.load())
                                               : 0.0;
        const guint64 min_packet_size = udp_input_runtime.udp_min_packet_size.load();
        const guint64 max_packet_size = udp_input_runtime.udp_max_packet_size.load();
        const double udp_frames_per_sec =
                udp_frames_decoded.load() > 0
                        ? static_cast<double>(udp_frames_decoded.load()) / static_cast<double>(total_runtime.count())
                        : 0.0;

        std::ostringstream udp_line_stream;
        udp_line_stream << "Packets: " << udp_packets << " (" << std::fixed << std::setprecision(2)
                        << udp_packets_per_sec << "/s)";
        udp_lines.push_back(udp_line_stream.str());
        udp_line_stream.str("");

        udp_line_stream << "Bytes: " << udp_bytes << " (" << std::fixed << std::setprecision(2)
                        << (static_cast<double>(udp_bytes) / (1024.0 * 1024.0)) << " MB)";
        udp_lines.push_back(udp_line_stream.str());
        udp_line_stream.str("");

        udp_line_stream << "Rate: " << std::fixed << std::setprecision(2) << udp_mbps << " Mbps";
        udp_lines.push_back(udp_line_stream.str());
        udp_line_stream.str("");

        udp_line_stream << "Avg Pkt: " << std::fixed << std::setprecision(2) << avg_packet_size << " bytes";
        udp_lines.push_back(udp_line_stream.str());
        udp_line_stream.str("");

        if (min_packet_size > 0 && max_packet_size > 0) {
            udp_line_stream << "Pkt Range: " << min_packet_size << "-" << max_packet_size << " bytes";
            udp_lines.push_back(udp_line_stream.str());
            udp_line_stream.str("");
        }

        udp_line_stream << "Frames: " << udp_frames_decoded.load() << " (" << std::fixed << std::setprecision(2)
                        << udp_frames_per_sec << " fps)";
        udp_lines.push_back(udp_line_stream.str());
    }

    // Build frame processing statistics lines
    std::vector<std::string> frame_lines;
    std::ostringstream frame_line_stream;
    // Processed: frames that went through ROI detection processing (may include dropped/bypassed frames)
    frame_line_stream << "Processed: " << frames_processed.load() << " (" << std::fixed << std::setprecision(2)
                      << total_fps << " fps)";
    frame_lines.push_back(frame_line_stream.str());
    frame_line_stream.str("");

    // Pushed to Encoder: frames successfully pushed to encoding pipeline (after ROI processing)
    const double pushed_fps =
            frames_pushed_to_encoder.load() > 0
                    ? static_cast<double>(frames_pushed_to_encoder.load()) / static_cast<double>(total_runtime.count())
                    : 0.0;
    frame_line_stream << "Pushed to Encoder: " << frames_pushed_to_encoder.load() << " (" << std::fixed
                      << std::setprecision(2) << pushed_fps << " fps)";
    frame_lines.push_back(frame_line_stream.str());
    frame_line_stream.str("");

    // Encoded: frames that were successfully encoded (excludes dropped frames)
    frame_line_stream << "Encoded: " << frames_encoded.load() << " (" << std::fixed << std::setprecision(2)
                      << encoding_fps << " fps)";
    frame_lines.push_back(frame_line_stream.str());
    frame_line_stream.str("");

    frame_line_stream << "Output: " << frames_output.load() << " (" << std::fixed << std::setprecision(2) << output_fps
                      << " fps)";
    frame_lines.push_back(frame_line_stream.str());
    frame_line_stream.str("");

    const double output_processed_fps =
            frames_output_processed.load() > 0
                    ? static_cast<double>(frames_output_processed.load()) / static_cast<double>(total_runtime.count())
                    : 0.0;
    frame_line_stream << "Output Processed: " << frames_output_processed.load() << " (" << std::fixed
                      << std::setprecision(2) << output_processed_fps << " fps)";
    frame_lines.push_back(frame_line_stream.str());
    frame_line_stream.str("");

    // Get dropped frames statistics (only available for UDP input)
    if (params.input_type_value == input_type::udp) {
        const auto drop_stats = udp_frame_drop_manager.get_statistics();
        if (drop_stats.total_frames_seen > 0) {
            const double drop_rate = (static_cast<double>(drop_stats.frames_dropped) /
                                      static_cast<double>(drop_stats.total_frames_seen)) *
                                     100.0;
            frame_line_stream << "Dropped: " << drop_stats.frames_dropped << " (" << std::fixed << std::setprecision(2)
                              << drop_rate << "%)";
            frame_lines.push_back(frame_line_stream.str());
            frame_line_stream.str("");
        }
    }

    // Get skipped frames statistics (bypass frames, only available when bypass is enabled)
    if (bypass.router) {
        const guint64 bypass_count = bypass.router->get_bypass_frame_count();
        const guint64 inference_count = bypass.router->get_inference_frame_count();
        const guint64 total_routed = bypass_count + inference_count;
        if (total_routed > 0) {
            const double bypass_rate = (static_cast<double>(bypass_count) / static_cast<double>(total_routed)) * 100.0;
            frame_line_stream << "Skipped (Bypass): " << bypass_count << " (" << std::fixed << std::setprecision(2)
                              << bypass_rate << "%)";
            frame_lines.push_back(frame_line_stream.str());
            frame_line_stream.str("");
        }
    }

    // Build output branch statistics lines
    // These show average FPS over total runtime (total frames / total time)
    std::vector<std::string> branch_lines;
    std::ostringstream branch_line_stream;

    const double display_fps = frames_output_display.load() > 0 ? static_cast<double>(frames_output_display.load()) /
                                                                          static_cast<double>(total_runtime.count())
                                                                : 0.0;
    branch_line_stream << "Display: " << frames_output_display.load() << " (" << std::fixed << std::setprecision(2)
                       << display_fps << " fps)";
    branch_lines.push_back(branch_line_stream.str());
    branch_line_stream.str("");

    const double h265_fps = frames_output_h265.load() > 0 ? static_cast<double>(frames_output_h265.load()) /
                                                                    static_cast<double>(total_runtime.count())
                                                          : 0.0;
    branch_line_stream << "H.265 File: " << frames_output_h265.load() << " (" << std::fixed << std::setprecision(2)
                       << h265_fps << " fps)";
    branch_lines.push_back(branch_line_stream.str());
    branch_line_stream.str("");

    const double mp4_fps = frames_output_mp4.load() > 0 ? static_cast<double>(frames_output_mp4.load()) /
                                                                  static_cast<double>(total_runtime.count())
                                                        : 0.0;
    branch_line_stream << "MP4 File: " << frames_output_mp4.load() << " (" << std::fixed << std::setprecision(2)
                       << mp4_fps << " fps)";
    branch_lines.push_back(branch_line_stream.str());
    branch_line_stream.str("");

    const double debug_fps = frames_output_debug.load() > 0 ? static_cast<double>(frames_output_debug.load()) /
                                                                      static_cast<double>(total_runtime.count())
                                                            : 0.0;
    branch_line_stream << "Debug: " << frames_output_debug.load() << " (" << std::fixed << std::setprecision(2)
                       << debug_fps << " fps)";
    branch_lines.push_back(branch_line_stream.str());
    branch_line_stream.str("");

    const double rtp_fps = frames_output_rtp.load() > 0 ? static_cast<double>(frames_output_rtp.load()) /
                                                                  static_cast<double>(total_runtime.count())
                                                        : 0.0;
    branch_line_stream << "RTP: " << frames_output_rtp.load() << " (" << std::fixed << std::setprecision(2) << rtp_fps
                       << " fps)";
    branch_lines.push_back(branch_line_stream.str());

    // Build ROI statistics lines
    std::vector<std::string> roi_lines;
    std::ostringstream roi_line_stream;
    roi_line_stream << "Detected: " << roi_objects_detected.load();
    roi_lines.push_back(roi_line_stream.str());
    roi_line_stream.str("");

    roi_line_stream << "Tracked: " << roi_objects_tracked.load();
    roi_lines.push_back(roi_line_stream.str());
    roi_line_stream.str("");

    roi_line_stream << "Filtered: " << roi_objects_filtered.load();
    roi_lines.push_back(roi_line_stream.str());
    roi_line_stream.str("");

    roi_line_stream << "SEI Injected: " << sei_messages_injected.load();
    roi_lines.push_back(roi_line_stream.str());
    roi_line_stream.str("");

    if (params.enable_adaptive_luma_adaptation) {
        roi_line_stream << "Adaptive luma: on";
        roi_lines.push_back(roi_line_stream.str());
        roi_line_stream.str("");
    }

    if (params.enable_luma_adaptation) {
        const double downscale_factor = luma_downscale.load();
        const double upscale_factor = luma_upscale.load();
        roi_line_stream << "Luma (d,u) applied: " << std::fixed << std::setprecision(2) << downscale_factor << ", "
                        << upscale_factor;
        roi_lines.push_back(roi_line_stream.str());
        roi_line_stream.str("");
    }

    const auto fallback_counters = memory_runtime.snapshot_counters();
    roi_line_stream << "Memory fallback (domain,writable,fail): "
                    << fallback_counters.memory_domain_fallback_conversions << ", "
                    << fallback_counters.writable_buffer_fallback_conversions << ", "
                    << fallback_counters.fallback_failures;
    roi_lines.push_back(roi_line_stream.str());
    roi_line_stream.str("");

    const auto metadata_counters = metadata_runtime.snapshot_counters();
    roi_line_stream << "Metadata contract (enq,deq,mismatch,invalid,fail-stop): " << metadata_counters.bundles_enqueued
                    << ", " << metadata_counters.bundles_dequeued << ", " << metadata_counters.sequence_mismatches
                    << ", " << metadata_counters.metadata_validation_failures << ", "
                    << metadata_counters.contract_fail_stops;
    roi_lines.push_back(roi_line_stream.str());
    roi_line_stream.str("");

    // Build pipeline FPS statistics lines (only for active pipelines)
    // These show rolling window FPS (most recent window, typically 1-2 seconds)
    // This differs from OUTPUT BRANCHES FPS which shows average over total runtime
    std::vector<std::string> pipeline_fps_lines;
    std::ostringstream pipeline_fps_line_stream;

    // Display pipeline FPS
    if (params.debug.enable_display_branch && fps_monitor.is_pipeline_active("display")) {
        const double display_pipeline_fps = fps_monitor.get_current_fps("display");
        pipeline_fps_line_stream << "Display: " << std::fixed << std::setprecision(2) << display_pipeline_fps << " fps";
        pipeline_fps_lines.push_back(pipeline_fps_line_stream.str());
        pipeline_fps_line_stream.str("");
    }

    // RTP pipeline FPS (check if RTP transport is active)
    if (params.communication.resolved_transport_type() == sf::communication::transport_type::udp &&
        fps_monitor.is_pipeline_active("rtp")) {
        const double rtp_pipeline_fps = fps_monitor.get_current_fps("rtp");
        pipeline_fps_line_stream << "RTP: " << std::fixed << std::setprecision(2) << rtp_pipeline_fps << " fps";
        pipeline_fps_lines.push_back(pipeline_fps_line_stream.str());
        pipeline_fps_line_stream.str("");
    }

    // WebRTC pipeline FPS
    if (params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc &&
        fps_monitor.is_pipeline_active("webrtc")) {
        const double webrtc_pipeline_fps = fps_monitor.get_current_fps("webrtc");
        pipeline_fps_line_stream << "WebRTC: " << std::fixed << std::setprecision(2) << webrtc_pipeline_fps << " fps";
        pipeline_fps_lines.push_back(pipeline_fps_line_stream.str());
        pipeline_fps_line_stream.str("");
    }

    // Calculate maximum width for each column
    size_t udp_max_width = 0;
    size_t frame_max_width = 0;
    size_t branch_max_width = 0;
    size_t roi_max_width = 0;
    size_t pipeline_fps_max_width = 0;

    for (const auto &udp_line: udp_lines) {
        udp_max_width = std::max(udp_max_width, udp_line.length());
    }
    for (const auto &frame_line: frame_lines) {
        frame_max_width = std::max(frame_max_width, frame_line.length());
    }
    for (const auto &branch_line: branch_lines) {
        branch_max_width = std::max(branch_max_width, branch_line.length());
    }
    for (const auto &roi_line: roi_lines) {
        roi_max_width = std::max(roi_max_width, roi_line.length());
    }
    for (const auto &pipeline_fps_line: pipeline_fps_lines) {
        pipeline_fps_max_width = std::max(pipeline_fps_max_width, pipeline_fps_line.length());
    }

    constexpr size_t column_spacing = 3; // Space between columns

    // Output titles horizontally, aligned above their columns
    status_report << "\n  ";
    if (params.input_type_value == input_type::udp) {
        status_report << "UDP STATISTICS:";
        const size_t udp_title_padding = udp_max_width > 14 ? udp_max_width - 14 : 0;
        status_report << std::string(udp_title_padding, ' ');
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        status_report << "FRAME PROCESSING:";
        const size_t frame_title_padding = frame_max_width > 17 ? frame_max_width - 17 : 0;
        status_report << std::string(frame_title_padding, ' ');
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        status_report << "OUTPUT BRANCHES:";
        const size_t branch_title_padding = branch_max_width > 14 ? branch_max_width - 14 : 0;
        status_report << std::string(branch_title_padding, ' ');
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        status_report << "ROI PROCESSING:";
        const size_t roi_title_padding = roi_max_width > 13 ? roi_max_width - 13 : 0;
        status_report << std::string(roi_title_padding, ' ');
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        if (!pipeline_fps_lines.empty()) {
            status_report << "PIPELINE FPS:";
            const size_t pipeline_fps_title_padding = pipeline_fps_max_width > 12 ? pipeline_fps_max_width - 12 : 0;
            status_report << std::string(pipeline_fps_title_padding, ' ');
        }
    } else {
        status_report << "FRAME PROCESSING:";
        const size_t frame_title_padding = frame_max_width > 17 ? frame_max_width - 17 : 0;
        status_report << std::string(frame_title_padding, ' ');
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        status_report << "OUTPUT BRANCHES:";
        const size_t branch_title_padding = branch_max_width > 14 ? branch_max_width - 14 : 0;
        status_report << std::string(branch_title_padding, ' ');
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        status_report << "ROI PROCESSING:";
        const size_t roi_title_padding = roi_max_width > 13 ? roi_max_width - 13 : 0;
        status_report << std::string(roi_title_padding, ' ');
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        if (!pipeline_fps_lines.empty()) {
            status_report << "PIPELINE FPS:";
            const size_t pipeline_fps_title_padding = pipeline_fps_max_width > 12 ? pipeline_fps_max_width - 12 : 0;
            status_report << std::string(pipeline_fps_title_padding, ' ');
        }
    }

    // Output content vertically below titles with alignment
    const size_t max_lines = std::max(
            {udp_lines.size(), frame_lines.size(), branch_lines.size(), roi_lines.size(), pipeline_fps_lines.size()});
    for (size_t i = 0; i < max_lines; ++i) {
        status_report << "\n  ";
        if (params.input_type_value == input_type::udp) {
            if (i < udp_lines.size()) {
                status_report << udp_lines[i];
                // Pad to align column
                const size_t padding = udp_max_width - udp_lines[i].length();
                status_report << std::string(padding, ' ');
            } else {
                status_report << std::string(udp_max_width, ' ');
            }
            status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');
        }
        if (i < frame_lines.size()) {
            status_report << frame_lines[i];
            // Pad to align column
            const size_t padding = frame_max_width - frame_lines[i].length();
            status_report << std::string(padding, ' ');
        } else {
            status_report << std::string(frame_max_width, ' ');
        }
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        if (i < branch_lines.size()) {
            status_report << branch_lines[i];
            // Pad to align column
            const size_t padding = branch_max_width - branch_lines[i].length();
            status_report << std::string(padding, ' ');
        } else {
            status_report << std::string(branch_max_width, ' ');
        }
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        if (i < roi_lines.size()) {
            status_report << roi_lines[i];
            // Pad to align column
            const size_t padding = roi_max_width - roi_lines[i].length();
            status_report << std::string(padding, ' ');
        } else {
            status_report << std::string(roi_max_width, ' ');
        }
        status_report << std::string(column_spacing, ' ') << "|" << std::string(column_spacing, ' ');

        if (i < pipeline_fps_lines.size()) {
            status_report << pipeline_fps_lines[i];
            // Pad to align column
            const size_t padding = pipeline_fps_max_width - pipeline_fps_lines[i].length();
            status_report << std::string(padding, ' ');
        } else if (!pipeline_fps_lines.empty()) {
            status_report << std::string(pipeline_fps_max_width, ' ');
        }
    }

    // Error reporting
    if (processing_errors.load() > 0) {
        status_report << "\n\nERRORS:";
        status_report << "\n  Processing Errors: " << processing_errors.load();
    }

    status_report << "\n=================================";
    return status_report.str();
}

void simple_status_reporter::generate_status_report() const {
    output_status(sf::log_level::info, build_status_report_string());
}

void simple_status_reporter::start_periodic_reporting(const int interval_seconds) {
    reporting_interval_seconds.store(interval_seconds);

    // For UDP mode, wait for client connection before starting reporting
    if (params.input_type_value == input_type::udp) {
        sf::info(simple_status_reporter_category,
                 "UDP mode detected: Waiting for client connection before starting status reporting...");

        // Wait indefinitely for UDP client to connect, but check for shutdown signal
        constexpr int check_interval_ms = 100;
        while (!udp_input_runtime.first_udp_packet_received.load() && !stop_processing.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        }

        if (stop_processing.load()) {
            sf::info(simple_status_reporter_category, "Shutdown requested: Exiting without starting status reporting");
            return;
        }

        sf::info(simple_status_reporter_category, "UDP client connected: Starting periodic status reporting");
        // Reset start time to when client connected, not when application started
        start_time = std::chrono::steady_clock::now();
    }

    periodic_reporting_active.store(true);

    sf::info(simple_status_reporter_category, "Periodic status reporting armed (interval: %d seconds); GstTask driver",
             interval_seconds);
}

void simple_status_reporter::stop_periodic_reporting() {
    periodic_reporting_active.store(false);
    sf::info(simple_status_reporter_category, "Periodic status reporting disarmed");
}

void simple_status_reporter::output_status(sf::log_level level, const std::string &message) {
    // Use GStreamer debug macros for consistent output
    if (level == sf::log_level::error) {
        sf::error(simple_status_reporter_category, "%s", message.c_str());
    } else if (level == sf::log_level::warning) {
        sf::warning(simple_status_reporter_category, "%s", message.c_str());
    } else {
        sf::info(simple_status_reporter_category, "%s", message.c_str());
    }
}
