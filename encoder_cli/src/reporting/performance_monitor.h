// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../libvcai/include/logging.h"
#include "../bridge/context/ingress_bridge_runtime.h"
#include "../bridge/context/memory_policy_runtime.h"
#include "../bridge/context/metadata_contract_runtime.h"
#include "../encoder_config.h"
#include "../global_data_instance_runtime.h"

/**
 * @file performance_monitor.h
 * @brief Performance monitoring system for tracking frame processing times through pipeline stages.
 */

/**
 * @enum monitoring_stage
 * @brief Identifies different pipeline stages to monitor.
 */
enum class monitoring_stage {
    inference, // DeepStream inference (total)
    inference_streammux, // nvstreammux processing
    inference_nvinfer, // nvinfer execution
    inference_tracker, // nvtracker processing
    inference_conversion, // nvvideoconvert operations
    roi_detection_processing, // ROI detection thread
    encoding, // GStreamer encoder
    encoded_frame_processing, // SEI injection thread
    input_decoding, // Input pipeline
    total_pipeline // End-to-end (if PTS tracking possible)
};

/**
 * @brief Gets the string name for a monitoring stage.
 * @param stage The monitoring stage.
 * @return String name of the stage.
 */
[[nodiscard]] const char *get_stage_name(monitoring_stage stage);

/**
 * @struct timing_stats
 * @brief Thread-safe statistics for a single pipeline stage.
 */
struct timing_stats {
    // Cumulative statistics (entire run)
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> total_time_ns{0};
    std::atomic<uint64_t> min_time_ns{UINT64_MAX};
    std::atomic<uint64_t> max_time_ns{0};

    // For calculating percentiles (rolling window - last N measurements)
    std::vector<uint64_t> recent_times; // Last N measurements for percentile calculation
    mutable std::mutex recent_times_mutex;
    static constexpr size_t recent_window_size = 1000; // Keep last 1000 measurements

    // Statistics for the last reporting interval (for comparison)
    uint64_t last_interval_frame_count{0};
    uint64_t last_interval_total_time_ns{0};
    std::chrono::steady_clock::time_point last_report_time;

    // Default constructor
    timing_stats() : recent_times(), recent_times_mutex(), last_report_time(std::chrono::steady_clock::now()) {}
};

/**
 * @struct performance_monitor
 * @brief Provides performance monitoring for pipeline stages.
 */
struct performance_monitor {
    /**
     * @brief Constructs a performance monitor bound to the narrow monitoring slice.
     * Each reference must outlive the monitor.
     * @param params Encoder configuration.
     * @param ingress_runtime Ingress bridge runtime (PTS storage queue size).
     * @param memory_runtime Memory-domain fallback counters.
     * @param metadata_runtime Metadata contract counters.
     * @param stop_processing Application stop flag.
     * @param udp_input_runtime UDP ingress packet counters.
     */
    explicit performance_monitor(const encoder_config &params, ingress_bridge_runtime &ingress_runtime,
                                 memory_policy_runtime &memory_runtime, metadata_contract_runtime &metadata_runtime,
                                 std::atomic_bool &stop_processing, udp_input_runtime_state &udp_input_runtime);

    /**
     * @brief Destructor.
     */
    ~performance_monitor();

    // Non-copyable
    performance_monitor(const performance_monitor &) = delete;
    performance_monitor &operator=(const performance_monitor &) = delete;

    /**
     * @brief Records timing for a pipeline stage.
     * @param stage The stage being monitored.
     * @param duration_ns Duration in nanoseconds.
     */
    void record_timing(monitoring_stage stage, uint64_t duration_ns);

    /**
     * @brief Records start time for a frame (for GStreamer stages).
     * @param stage The stage being monitored.
     * @param pts Frame PTS used as key for matching start/end times.
     */
    void record_start_time(monitoring_stage stage, GstClockTime pts);

    /**
     * @brief Records end time for a frame (for GStreamer stages).
     * @param stage The stage being monitored.
     * @param pts Frame PTS used as key for matching start/end times.
     */
    void record_end_time(monitoring_stage stage, GstClockTime pts);

    /**
     * @brief Starts periodic performance reporting.
     * @param interval_seconds Interval between reports in seconds.
     */
    void start_periodic_reporting(int interval_seconds = 5);

    /**
     * @brief Stops periodic performance reporting.
     */
    void stop_periodic_reporting();

    [[nodiscard]] bool is_periodic_reporting_active() const { return periodic_reporting_active.load(); }

    [[nodiscard]] int get_reporting_interval_seconds() const { return reporting_interval_seconds.load(); }

    /**
     * @brief Generates a periodic report (cumulative + recent stats).
     */
    void generate_periodic_report() const;

    /**
     * @brief Resets per-interval baselines after a periodic report (GstTask driver).
     */
    void update_periodic_report_interval_baseline();

    /**
     * @brief Generates final summary report at shutdown.
     */
    void generate_final_summary() const;

private:
    const encoder_config &params;
    ingress_bridge_runtime &ingress_runtime;
    memory_policy_runtime &memory_runtime;
    metadata_contract_runtime &metadata_runtime;
    std::atomic_bool &stop_processing;
    udp_input_runtime_state &udp_input_runtime;
    std::chrono::steady_clock::time_point start_time;
    std::array<timing_stats, 10> stage_stats; // One for each monitoring_stage
    std::atomic<bool> periodic_reporting_active{false};
    std::atomic<int> reporting_interval_seconds{5};

    // For GStreamer stages: map PTS to start time
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> inference_start_times;
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> inference_streammux_start_times;
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> inference_nvinfer_start_times;
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> inference_tracker_start_times;
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> inference_conversion_start_times;
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> encoding_start_times;
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> input_decoding_start_times;
    // For Total Pipeline: use PTS-based map for accurate tracking
    std::unordered_map<GstClockTime, std::chrono::steady_clock::time_point> total_pipeline_start_times;
    std::mutex inference_times_mutex;
    std::mutex inference_streammux_times_mutex;
    std::mutex inference_nvinfer_times_mutex;
    std::mutex inference_tracker_times_mutex;
    std::mutex inference_conversion_times_mutex;
    std::mutex encoding_times_mutex;
    std::mutex input_decoding_times_mutex;
    std::mutex total_pipeline_times_mutex;

    /**
     * @brief Result of cumulative statistics calculation for a pipeline stage.
     */
    struct cumulative_stats_result {
        double avg_time_ns{0.0};
        uint64_t min_time_ns{0};
        uint64_t max_time_ns{0};
        uint64_t total_frames{0};
    };

    /**
     * @brief Calculates cumulative statistics for a stage.
     * @param stage The stage to calculate stats for.
     * @return Cumulative statistics for the stage.
     */
    [[nodiscard]] cumulative_stats_result calculate_cumulative_stats(monitoring_stage stage) const;

    /**
     * @brief Result of recent window statistics calculation (percentiles).
     */
    struct recent_stats_result {
        double p50_ns{0.0};
        double p95_ns{0.0};
        double p99_ns{0.0};
    };

    /**
     * @brief Calculates recent window statistics (percentiles).
     * @param stage The stage to calculate stats for.
     * @return Recent window percentile statistics.
     */
    [[nodiscard]] recent_stats_result calculate_recent_stats(monitoring_stage stage) const;

    /**
     * @brief Cleans up stale entries from a PTS-based start times map.
     * @param map The map to clean up.
     * @param now Current time point.
     * @param mutex The mutex protecting the map (must already be locked).
     */
    template<typename MapType>
    void cleanup_stale_entries(MapType &map, const std::chrono::steady_clock::time_point &now,
                               [[maybe_unused]] std::mutex &mutex) {
        const auto cutoff = now - std::chrono::seconds(5);
        for (auto it = map.begin(); it != map.end();) {
            if (it->second < cutoff) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    }

    /**
     * @brief Result of interval statistics calculation (last reporting period).
     */
    struct interval_stats_result {
        uint64_t interval_frames{0};
        double interval_avg_ns{0.0};
        double interval_fps{0.0};
    };

    /**
     * @brief Calculates interval statistics (last reporting period).
     * @param stage The stage to calculate stats for.
     * @return Interval statistics for the stage.
     */
    [[nodiscard]] interval_stats_result calculate_interval_stats(monitoring_stage stage) const;

    /**
     * @brief Outputs a monitoring message.
     * @param level Log level enum value.
     * @param message Monitoring message.
     */
    static void output_monitoring(sf::log_level level, const std::string &message);
};

/**
 * @brief Initializes the GStreamer debug category for performance monitoring.
 */
void init_performance_monitor_debug_category();
