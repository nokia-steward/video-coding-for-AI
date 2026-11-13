// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "performance_monitor.h"
#include "../encoder_enums.h"
#include "../utils/gpu_monitor.h"
#include "../utils/memory_monitor.h"

#include <algorithm>
#include <array>
#include <gst/gst.h>
#include <iomanip>
#include <sstream>
#include <thread>

/**
 * @file performance_monitor.cpp
 * @brief Implementation of the performance monitoring system.
 */

/// GStreamer debug category for performance monitoring output.
GST_DEBUG_CATEGORY_STATIC(performance_monitor_category);

void init_performance_monitor_debug_category() {
    GST_DEBUG_CATEGORY_INIT(performance_monitor_category, "performance-monitor", 0,
                            "Performance monitoring and timing statistics");
}

const char *get_stage_name(monitoring_stage stage) {
    switch (stage) {
        case monitoring_stage::inference:
            return "Inference";
        case monitoring_stage::inference_streammux:
            return "Inference Streammux";
        case monitoring_stage::inference_nvinfer:
            return "Inference Nvinfer";
        case monitoring_stage::inference_tracker:
            return "Inference Tracker";
        case monitoring_stage::inference_conversion:
            return "Inference Conversion";
        case monitoring_stage::roi_detection_processing:
            return "ROI Detection Processing";
        case monitoring_stage::encoding:
            return "Encoding";
        case monitoring_stage::encoded_frame_processing:
            return "Encoded Frame Processing";
        case monitoring_stage::input_decoding:
            return "Input Decoding";
        case monitoring_stage::total_pipeline:
            return "Total Pipeline";
        default:
            return "Unknown";
    }
}

namespace {
    [[nodiscard]] uint64_t pts_storage_queue_size_callback(const void *callback_context) {
        const auto *runtime = static_cast<const ingress_bridge_runtime *>(callback_context);
        return runtime != nullptr ? runtime->pts_queue_size() : 0;
    }

    [[nodiscard]] memory_fallback_counters memory_fallback_counters_callback(const void *callback_context) {
        const auto *runtime = static_cast<const memory_policy_runtime *>(callback_context);
        return runtime != nullptr ? runtime->snapshot_counters() : memory_fallback_counters{};
    }
} // namespace

performance_monitor::performance_monitor(const encoder_config &params_ref, ingress_bridge_runtime &ingress_runtime_ref,
                                         memory_policy_runtime &memory_runtime_ref,
                                         metadata_contract_runtime &metadata_runtime_ref,
                                         std::atomic_bool &stop_processing_ref,
                                         udp_input_runtime_state &udp_input_runtime_ref) :
    params(params_ref),
    ingress_runtime(ingress_runtime_ref), memory_runtime(memory_runtime_ref), metadata_runtime(metadata_runtime_ref),
    stop_processing(stop_processing_ref), udp_input_runtime(udp_input_runtime_ref),
    start_time(std::chrono::steady_clock::now()), stage_stats(), inference_start_times(),
    inference_streammux_start_times(), inference_nvinfer_start_times(), inference_tracker_start_times(),
    inference_conversion_start_times(), encoding_start_times(), input_decoding_start_times(),
    total_pipeline_start_times(), inference_times_mutex(), inference_streammux_times_mutex(),
    inference_nvinfer_times_mutex(), inference_tracker_times_mutex(), inference_conversion_times_mutex(),
    encoding_times_mutex(), input_decoding_times_mutex(), total_pipeline_times_mutex() {
    // Initialize last_report_time for all stages
    const auto now = std::chrono::steady_clock::now();
    for (auto &stats: stage_stats) {
        stats.last_report_time = now;
    }

    // Set up PTS storage queue size callback for memory diagnostics (per-instance registration).
    register_pts_storage_queue_size_callback(pts_storage_queue_size_callback, &ingress_runtime);
    register_memory_fallback_counters_callback(memory_fallback_counters_callback, &memory_runtime);

    sf::info(performance_monitor_category, "Performance monitor initialized");
}

performance_monitor::~performance_monitor() {
    stop_periodic_reporting();
    unregister_pts_storage_queue_size_callback(&ingress_runtime);
    unregister_memory_fallback_counters_callback(&memory_runtime);
    sf::info(performance_monitor_category, "Performance monitor destroyed");
}

void performance_monitor::record_timing(monitoring_stage stage, const uint64_t duration_ns) {
    if (static_cast<size_t>(stage) >= stage_stats.size()) {
        return;
    }

    auto &stats = stage_stats[static_cast<size_t>(stage)];

    // Update cumulative statistics
    stats.frame_count.fetch_add(1);
    stats.total_time_ns.fetch_add(duration_ns);

    // Update min/max atomically
    uint64_t current_min = stats.min_time_ns.load();
    while (duration_ns < current_min && !stats.min_time_ns.compare_exchange_weak(current_min, duration_ns)) {
        // Retry if compare_exchange_weak failed
    }

    uint64_t current_max = stats.max_time_ns.load();
    while (duration_ns > current_max && !stats.max_time_ns.compare_exchange_weak(current_max, duration_ns)) {
        // Retry if compare_exchange_weak failed
    }

    // Update recent times for percentile calculation
    {
        std::lock_guard<std::mutex> lock(stats.recent_times_mutex);
        stats.recent_times.push_back(duration_ns);
        // Keep only last N measurements
        if (stats.recent_times.size() > timing_stats::recent_window_size) {
            stats.recent_times.erase(stats.recent_times.begin());
        }
    }
}

void performance_monitor::record_start_time(const monitoring_stage stage, const GstClockTime pts) {
    const auto now = std::chrono::steady_clock::now();

    if (stage == monitoring_stage::inference) {
        std::lock_guard<std::mutex> lock(inference_times_mutex);
        inference_start_times[pts] = now;
        // Clean up stale entries older than 5 seconds to prevent unbounded growth
        cleanup_stale_entries(inference_start_times, now, inference_times_mutex);
    } else if (stage == monitoring_stage::inference_streammux) {
        std::lock_guard<std::mutex> lock(inference_streammux_times_mutex);
        inference_streammux_start_times[pts] = now;
        cleanup_stale_entries(inference_streammux_start_times, now, inference_streammux_times_mutex);
    } else if (stage == monitoring_stage::inference_nvinfer) {
        std::lock_guard<std::mutex> lock(inference_nvinfer_times_mutex);
        inference_nvinfer_start_times[pts] = now;
        cleanup_stale_entries(inference_nvinfer_start_times, now, inference_nvinfer_times_mutex);
    } else if (stage == monitoring_stage::inference_tracker) {
        std::lock_guard<std::mutex> lock(inference_tracker_times_mutex);
        inference_tracker_start_times[pts] = now;
        cleanup_stale_entries(inference_tracker_start_times, now, inference_tracker_times_mutex);
    } else if (stage == monitoring_stage::inference_conversion) {
        std::lock_guard<std::mutex> lock(inference_conversion_times_mutex);
        inference_conversion_start_times[pts] = now;
        cleanup_stale_entries(inference_conversion_start_times, now, inference_conversion_times_mutex);
    } else if (stage == monitoring_stage::encoding) {
        std::lock_guard<std::mutex> lock(encoding_times_mutex);
        encoding_start_times[pts] = now;
        // Clean up stale entries older than 5 seconds to prevent unbounded growth
        cleanup_stale_entries(encoding_start_times, now, encoding_times_mutex);
    } else if (stage == monitoring_stage::input_decoding) {
        std::lock_guard<std::mutex> lock(input_decoding_times_mutex);
        input_decoding_start_times[pts] = now;
        // Clean up stale entries older than 5 seconds to prevent unbounded growth
        cleanup_stale_entries(input_decoding_start_times, now, input_decoding_times_mutex);
    } else if (stage == monitoring_stage::total_pipeline) {
        std::lock_guard<std::mutex> lock(total_pipeline_times_mutex);
        total_pipeline_start_times[pts] = now;
        // Clean up stale entries older than 5 seconds to prevent unbounded growth
        cleanup_stale_entries(total_pipeline_start_times, now, total_pipeline_times_mutex);
    }
}

void performance_monitor::record_end_time(const monitoring_stage stage, const GstClockTime pts) {
    std::chrono::steady_clock::time_point start_time_point;
    bool found = false;

    if (stage == monitoring_stage::inference) {
        std::lock_guard<std::mutex> lock(inference_times_mutex);
        const auto it = inference_start_times.find(pts);
        if (it != inference_start_times.end()) {
            start_time_point = it->second;
            inference_start_times.erase(it);
            found = true;
        }
    } else if (stage == monitoring_stage::inference_streammux) {
        std::lock_guard<std::mutex> lock(inference_streammux_times_mutex);
        const auto it = inference_streammux_start_times.find(pts);
        if (it != inference_streammux_start_times.end()) {
            start_time_point = it->second;
            inference_streammux_start_times.erase(it);
            found = true;
        }
    } else if (stage == monitoring_stage::inference_nvinfer) {
        std::lock_guard<std::mutex> lock(inference_nvinfer_times_mutex);
        const auto it = inference_nvinfer_start_times.find(pts);
        if (it != inference_nvinfer_start_times.end()) {
            start_time_point = it->second;
            inference_nvinfer_start_times.erase(it);
            found = true;
        }
    } else if (stage == monitoring_stage::inference_tracker) {
        std::lock_guard<std::mutex> lock(inference_tracker_times_mutex);
        const auto it = inference_tracker_start_times.find(pts);
        if (it != inference_tracker_start_times.end()) {
            start_time_point = it->second;
            inference_tracker_start_times.erase(it);
            found = true;
        }
    } else if (stage == monitoring_stage::inference_conversion) {
        std::lock_guard<std::mutex> lock(inference_conversion_times_mutex);
        const auto it = inference_conversion_start_times.find(pts);
        if (it != inference_conversion_start_times.end()) {
            start_time_point = it->second;
            inference_conversion_start_times.erase(it);
            found = true;
        }
    } else if (stage == monitoring_stage::encoding) {
        std::lock_guard<std::mutex> lock(encoding_times_mutex);
        const auto it = encoding_start_times.find(pts);
        if (it != encoding_start_times.end()) {
            start_time_point = it->second;
            encoding_start_times.erase(it);
            found = true;
        }
    } else if (stage == monitoring_stage::input_decoding) {
        std::lock_guard<std::mutex> lock(input_decoding_times_mutex);
        const auto it = input_decoding_start_times.find(pts);
        if (it != input_decoding_start_times.end()) {
            start_time_point = it->second;
            input_decoding_start_times.erase(it);
            found = true;
        }
    } else if (stage == monitoring_stage::total_pipeline) {
        std::lock_guard<std::mutex> lock(total_pipeline_times_mutex);
        const auto it = total_pipeline_start_times.find(pts);
        if (it != total_pipeline_start_times.end()) {
            start_time_point = it->second;
            total_pipeline_start_times.erase(it);
            found = true;
        }
    }

    if (found) {
        const auto end_time_point = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time_point - start_time_point);
        record_timing(stage, static_cast<uint64_t>(duration.count()));
    }
    // Removed debug message for "No start time found" - too verbose and expected in some cases
    // (e.g., frame drops, PTS mismatches in UDP input)
}

performance_monitor::cumulative_stats_result
performance_monitor::calculate_cumulative_stats(const monitoring_stage stage) const {
    cumulative_stats_result result{};

    if (static_cast<size_t>(stage) >= stage_stats.size()) {
        return result;
    }

    const auto &stats = stage_stats[static_cast<size_t>(stage)];
    result.total_frames = stats.frame_count.load();
    const uint64_t total_time = stats.total_time_ns.load();

    if (result.total_frames > 0) {
        result.avg_time_ns = static_cast<double>(total_time) / static_cast<double>(result.total_frames);
    }

    result.min_time_ns = stats.min_time_ns.load();
    result.max_time_ns = stats.max_time_ns.load();

    // Handle case where no frames have been processed yet
    if (result.min_time_ns == UINT64_MAX) {
        result.min_time_ns = 0;
    }

    return result;
}

performance_monitor::recent_stats_result
performance_monitor::calculate_recent_stats(const monitoring_stage stage) const {
    recent_stats_result result{};

    if (static_cast<size_t>(stage) >= stage_stats.size()) {
        return result;
    }

    const auto &stats = stage_stats[static_cast<size_t>(stage)];

    std::lock_guard<std::mutex> lock(stats.recent_times_mutex);
    if (stats.recent_times.empty()) {
        return result;
    }

    // Create a copy for sorting
    std::vector<uint64_t> sorted_times = stats.recent_times;
    std::sort(sorted_times.begin(), sorted_times.end());

    const size_t count = sorted_times.size();
    if (count > 0) {
        result.p50_ns = static_cast<double>(sorted_times[count * 50 / 100]);
        if (count > 1) {
            result.p95_ns = static_cast<double>(sorted_times[count * 95 / 100]);
            result.p99_ns = static_cast<double>(sorted_times[count * 99 / 100]);
        } else {
            result.p95_ns = result.p50_ns;
            result.p99_ns = result.p50_ns;
        }
    }

    return result;
}

performance_monitor::interval_stats_result
performance_monitor::calculate_interval_stats(const monitoring_stage stage) const {
    interval_stats_result result{};

    if (static_cast<size_t>(stage) >= stage_stats.size()) {
        return result;
    }

    const auto &stats = stage_stats[static_cast<size_t>(stage)];
    const uint64_t current_frames = stats.frame_count.load();
    const uint64_t current_total_time = stats.total_time_ns.load();

    result.interval_frames = current_frames - stats.last_interval_frame_count;
    const uint64_t interval_total_time = current_total_time - stats.last_interval_total_time_ns;

    if (result.interval_frames > 0) {
        result.interval_avg_ns = static_cast<double>(interval_total_time) / static_cast<double>(result.interval_frames);
    }

    const auto now = std::chrono::steady_clock::now();
    const auto interval_duration = std::chrono::duration_cast<std::chrono::seconds>(now - stats.last_report_time);
    const int64_t interval_seconds = interval_duration.count();

    if (interval_seconds > 0) {
        result.interval_fps = static_cast<double>(result.interval_frames) / static_cast<double>(interval_seconds);
    }

    return result;
}

void performance_monitor::generate_periodic_report() const {
    const auto now = std::chrono::steady_clock::now();
    const auto total_runtime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);

    std::ostringstream report;
    report << "=== PERFORMANCE MONITORING REPORT (Cumulative) ===";
    report << "\nRuntime: " << total_runtime.count() << "s\n";

    // Add GPU statistics if available
    const auto gpu_stats_opt = get_gpu_stats();
    if (gpu_stats_opt) {
        const auto &gpu_stats = *gpu_stats_opt;
        report << "\nGPU Statistics:";
        report << "\n  Utilization: " << std::fixed << std::setprecision(1) << gpu_stats.utilization_percent << "%";
        report << "\n  Memory: " << (gpu_stats.memory_used_bytes / (1024 * 1024)) << " MB / "
               << (gpu_stats.memory_total_bytes / (1024 * 1024)) << " MB (" << std::fixed << std::setprecision(1)
               << (100.0 * static_cast<double>(gpu_stats.memory_used_bytes) /
                   static_cast<double>(gpu_stats.memory_total_bytes))
               << "%)";
        report << "\n  Temperature: " << std::fixed << std::setprecision(1) << gpu_stats.temperature_celsius << "°C";
        report << "\n  Power: " << gpu_stats.power_watts << "W";
        report << "\n  Clocks: Graphics=" << gpu_stats.clock_graphics_mhz
               << "MHz, Memory=" << gpu_stats.clock_memory_mhz << "MHz";
        report << "\n";
    }

    // Add memory statistics for diagnostics.
    const auto memory_stats_opt = get_memory_stats();

    if (memory_stats_opt) {
        const auto &mem_stats = *memory_stats_opt;
        report << "\nMemory Statistics:";
        report << "\n  Host Memory: RSS=" << (mem_stats.process_rss_bytes / (1024 * 1024)) << " MB, "
               << "VSS=" << (mem_stats.process_vss_bytes / (1024 * 1024)) << " MB, "
               << "Peak RSS=" << (mem_stats.process_peak_rss_bytes / (1024 * 1024)) << " MB";
        if (mem_stats.gpu_memory_total_bytes > 0) {
            report << "\n  GPU Memory: " << (mem_stats.gpu_memory_used_bytes / (1024 * 1024)) << " MB / "
                   << (mem_stats.gpu_memory_total_bytes / (1024 * 1024)) << " MB (" << std::fixed
                   << std::setprecision(1) << mem_stats.gpu_memory_utilization_percent << "%)";
        }
        report << "\n  PTS Storage Queue Size: " << mem_stats.pts_storage_queue_size << " frames";
        if (mem_stats.pts_storage_queue_size > 1000) {
            report << " ⚠️ WARNING: Queue size > 1000 - potential memory leak";
        } else if (mem_stats.pts_storage_queue_size > 100) {
            report << " ⚠️ CAUTION: Queue size > 100 - monitor for growth";
        }
        report << "\n  Memory Fallbacks: domain=" << mem_stats.fallback_counters.memory_domain_fallback_conversions
               << ", writable=" << mem_stats.fallback_counters.writable_buffer_fallback_conversions
               << ", failures=" << mem_stats.fallback_counters.fallback_failures;
        const auto metadata_counters = metadata_runtime.snapshot_counters();
        report << "\n  Metadata Contract: enq=" << metadata_counters.bundles_enqueued
               << ", deq=" << metadata_counters.bundles_dequeued
               << ", mismatch=" << metadata_counters.sequence_mismatches
               << ", invalid=" << metadata_counters.metadata_validation_failures
               << ", fail-stop=" << metadata_counters.contract_fail_stops;
        report << "\n";
    }

    // Build cumulative statistics table
    report << "\n"
           << std::left << std::setw(24) << "STAGE"
           << " | " << std::right << std::setw(12) << "Total Frames"
           << " | " << std::setw(16) << "Avg (ms/frame)"
           << " | " << std::setw(16) << "Min (ms/frame)"
           << " | " << std::setw(16) << "Max (ms/frame)"
           << " | " << std::setw(16) << "P95 (ms/frame)"
           << " | " << std::setw(16) << "P99 (ms/frame)"
           << " | " << std::setw(12) << "Overall FPS";
    report << "\n"
           << std::string(24, '-') << " | " << std::string(12, '-') << " | " << std::string(16, '-') << " | "
           << std::string(16, '-') << " | " << std::string(16, '-') << " | " << std::string(16, '-') << " | "
           << std::string(16, '-') << " | " << std::string(12, '-');

    // Calculate statistics for each stage
    for (int stage_idx = 0; stage_idx < 10; ++stage_idx) {
        const auto stage = static_cast<monitoring_stage>(stage_idx);
        const auto cumulative = calculate_cumulative_stats(stage);
        const auto recent = calculate_recent_stats(stage);

        // Calculate overall FPS
        double overall_fps = 0.0;
        const int64_t runtime_seconds = total_runtime.count();
        if (runtime_seconds > 0 && cumulative.total_frames > 0) {
            overall_fps = static_cast<double>(cumulative.total_frames) / static_cast<double>(runtime_seconds);
        }

        // Convert nanoseconds to milliseconds
        const double avg_ms = cumulative.avg_time_ns / 1000000.0;
        const double min_ms = static_cast<double>(cumulative.min_time_ns) / 1000000.0;
        const double max_ms = static_cast<double>(cumulative.max_time_ns) / 1000000.0;
        const double p95_ms = recent.p95_ns / 1000000.0;
        const double p99_ms = recent.p99_ns / 1000000.0;

        report << "\n"
               << std::left << std::setw(24) << get_stage_name(stage) << " | " << std::right << std::setw(12)
               << cumulative.total_frames << " | " << std::fixed << std::setprecision(2) << std::setw(16) << avg_ms
               << " | " << std::setw(16) << min_ms << " | " << std::setw(16) << max_ms << " | " << std::setw(16)
               << p95_ms << " | " << std::setw(16) << p99_ms << " | " << std::setw(12) << overall_fps;
    }

    // Build interval statistics (last reporting period)
    report << "\n\n--- Last " << reporting_interval_seconds.load() << " seconds ---";
    report << "\n"
           << std::left << std::setw(24) << "STAGE"
           << " | " << std::right << std::setw(6) << "Frames"
           << " | " << std::setw(16) << "Avg (ms/frame)"
           << " | " << std::setw(3) << "FPS";
    report << "\n"
           << std::string(24, '-') << " | " << std::string(6, '-') << " | " << std::string(16, '-') << " | "
           << std::string(3, '-');

    for (int stage_idx = 0; stage_idx < 10; ++stage_idx) {
        const auto stage = static_cast<monitoring_stage>(stage_idx);
        const auto interval = calculate_interval_stats(stage);

        const double interval_avg_ms = interval.interval_avg_ns / 1000000.0;

        report << "\n"
               << std::left << std::setw(24) << get_stage_name(stage) << " | " << std::right << std::setw(6)
               << interval.interval_frames << " | " << std::fixed << std::setprecision(2) << std::setw(16)
               << interval_avg_ms << " | " << std::setw(3) << std::setprecision(1) << interval.interval_fps;
    }

    report << "\n===================================================";
    output_monitoring(sf::log_level::info, report.str());
}

void performance_monitor::generate_final_summary() const {
    const auto now = std::chrono::steady_clock::now();
    const auto total_runtime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);

    std::ostringstream summary;
    summary << "=== FINAL PERFORMANCE MONITORING SUMMARY ===";
    summary << "\nTotal Runtime: " << total_runtime.count() << "s\n";

    // Build final statistics table
    summary << "\n"
            << std::left << std::setw(24) << "STAGE"
            << " | " << std::right << std::setw(12) << "Total Frames"
            << " | " << std::setw(16) << "Avg (ms/frame)"
            << " | " << std::setw(16) << "Min (ms/frame)"
            << " | " << std::setw(16) << "Max (ms/frame)"
            << " | " << std::setw(16) << "P50 (ms/frame)"
            << " | " << std::setw(16) << "P95 (ms/frame)"
            << " | " << std::setw(16) << "P99 (ms/frame)"
            << " | " << std::setw(12) << "Overall FPS";
    summary << "\n"
            << std::string(24, '-') << " | " << std::string(12, '-') << " | " << std::string(16, '-') << " | "
            << std::string(16, '-') << " | " << std::string(16, '-') << " | " << std::string(16, '-') << " | "
            << std::string(16, '-') << " | " << std::string(16, '-') << " | " << std::string(12, '-');

    struct stage_summary {
        monitoring_stage stage;
        double avg_ms;
        uint64_t total_frames;
    };
    std::vector<stage_summary> summaries;

    // Calculate statistics for each stage
    for (int stage_idx = 0; stage_idx < 10; ++stage_idx) {
        const auto stage = static_cast<monitoring_stage>(stage_idx);
        const auto cumulative = calculate_cumulative_stats(stage);
        const auto recent = calculate_recent_stats(stage);

        // Calculate overall FPS
        double overall_fps = 0.0;
        const int64_t runtime_seconds = total_runtime.count();
        if (runtime_seconds > 0 && cumulative.total_frames > 0) {
            overall_fps = static_cast<double>(cumulative.total_frames) / static_cast<double>(runtime_seconds);
        }

        // Convert nanoseconds to milliseconds
        const double avg_ms = cumulative.avg_time_ns / 1000000.0;
        const double min_ms = static_cast<double>(cumulative.min_time_ns) / 1000000.0;
        const double max_ms = static_cast<double>(cumulative.max_time_ns) / 1000000.0;
        const double p50_ms = recent.p50_ns / 1000000.0;
        const double p95_ms = recent.p95_ns / 1000000.0;
        const double p99_ms = recent.p99_ns / 1000000.0;

        summary << "\n"
                << std::left << std::setw(24) << get_stage_name(stage) << " | " << std::right << std::setw(12)
                << cumulative.total_frames << " | " << std::fixed << std::setprecision(2) << std::setw(16) << avg_ms
                << " | " << std::setw(16) << min_ms << " | " << std::setw(16) << max_ms << " | " << std::setw(16)
                << p50_ms << " | " << std::setw(16) << p95_ms << " | " << std::setw(16) << p99_ms << " | "
                << std::setw(12) << std::setprecision(1) << overall_fps;

        if (cumulative.total_frames > 0) {
            summaries.push_back({stage, avg_ms, cumulative.total_frames});
        }
    }

    // Find slowest and fastest stages
    if (!summaries.empty()) {
        const auto slowest =
                std::max_element(summaries.begin(), summaries.end(),
                                 [](const stage_summary &a, const stage_summary &b) { return a.avg_ms < b.avg_ms; });
        const auto fastest =
                std::min_element(summaries.begin(), summaries.end(),
                                 [](const stage_summary &a, const stage_summary &b) { return a.avg_ms < b.avg_ms; });

        summary << "\n\nPerformance Summary:";
        summary << "\n- Slowest stage: " << get_stage_name(slowest->stage) << " (avg " << std::fixed
                << std::setprecision(2) << slowest->avg_ms << "ms)";
        summary << "\n- Fastest stage: " << get_stage_name(fastest->stage) << " (avg " << std::fixed
                << std::setprecision(2) << fastest->avg_ms << "ms)";

        // Find total pipeline latency if available
        const auto total_pipeline_stats = calculate_cumulative_stats(monitoring_stage::total_pipeline);
        if (total_pipeline_stats.total_frames > 0) {
            const double total_avg_ms = total_pipeline_stats.avg_time_ns / 1000000.0;
            const double total_max_ms = static_cast<double>(total_pipeline_stats.max_time_ns) / 1000000.0;
            summary << "\n- Total pipeline latency: " << std::fixed << std::setprecision(2) << total_avg_ms
                    << "ms average";
            summary << "\n- Peak pipeline latency: " << std::fixed << std::setprecision(2) << total_max_ms << "ms";
        }
    }

    summary << "\n===========================================";
    output_monitoring(sf::log_level::info, summary.str());
}

void performance_monitor::start_periodic_reporting(const int interval_seconds) {
    reporting_interval_seconds.store(interval_seconds);

    // For UDP mode, wait for client connection before starting reporting (similar to status reporter)
    if (params.input_type_value == input_type::udp) {
        sf::info(performance_monitor_category,
                 "UDP mode detected: Waiting for client connection before starting performance monitoring...");

        // Wait indefinitely for UDP client to connect, but check for shutdown signal
        constexpr int check_interval_ms = 100;
        while (!udp_input_runtime.first_udp_packet_received.load() && !stop_processing.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        }

        if (stop_processing.load()) {
            sf::info(performance_monitor_category,
                     "Shutdown requested: Exiting without starting performance monitoring");
            return;
        }

        sf::info(performance_monitor_category, "UDP client connected: Starting periodic performance monitoring");
        // Reset start time to when client connected, not when application started
        start_time = std::chrono::steady_clock::now();
    }

    periodic_reporting_active.store(true);

    sf::info(performance_monitor_category,
             "Periodic performance monitoring armed (interval: %d seconds); GstTask driver", interval_seconds);
}

void performance_monitor::update_periodic_report_interval_baseline() {
    const auto now = std::chrono::steady_clock::now();
    for (auto &stats: stage_stats) {
        stats.last_interval_frame_count = stats.frame_count.load();
        stats.last_interval_total_time_ns = stats.total_time_ns.load();
        stats.last_report_time = now;
    }
}

void performance_monitor::stop_periodic_reporting() {
    periodic_reporting_active.store(false);
    sf::info(performance_monitor_category, "Periodic performance monitoring disarmed");
}

void performance_monitor::output_monitoring(const sf::log_level level, const std::string &message) {
    // Use GStreamer debug macros for consistent output
    if (level == sf::log_level::error) {
        sf::error(performance_monitor_category, "%s", message.c_str());
    } else if (level == sf::log_level::warning) {
        sf::warning(performance_monitor_category, "%s", message.c_str());
    } else {
        sf::info(performance_monitor_category, "%s", message.c_str());
    }
}
