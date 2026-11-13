// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include "../../libvcai/include/logging.h"
#include "../../libvcai/include/pipeline_fps_monitor.h"
#include "../bridge/context/memory_policy_runtime.h"
#include "../bridge/context/metadata_contract_runtime.h"
#include "../encoder_config.h"
#include "../global_data_instance_runtime.h"
#include "../gst/input_types/frame_drop_manager.h"
#include "../runtime/bypass_state.h"

/**
 * @file simple_status_reporter.h
 * @brief A simplified status reporting system for the encoder application.
 */

/**
 * @class simple_status_reporter
 * @brief Provides basic status reporting for the encoder application.
 */
struct simple_status_reporter {
    /**
     * @brief Constructs a simple status reporter bound to the narrow reporting slice.
     * Each reference must outlive the reporter.
     * @param params Encoder configuration.
     * @param bypass Bypass routing state.
     * @param memory_runtime Memory-domain fallback counters.
     * @param metadata_runtime Metadata contract counters.
     * @param fps_monitor Pipeline FPS monitor.
     * @param stop_processing Application stop flag.
     * @param udp_input_runtime UDP ingress packet counters.
     * @param udp_frame_drop_manager UDP decoder frame-drop counters.
     * @param luma_downscale Last applied luma downscale factor.
     * @param luma_upscale Last applied luma upscale factor.
     */
    explicit simple_status_reporter(const encoder_config &params, bypass_state &bypass,
                                    memory_policy_runtime &memory_runtime, metadata_contract_runtime &metadata_runtime,
                                    sf::pipeline_fps_monitor &fps_monitor, std::atomic_bool &stop_processing,
                                    udp_input_runtime_state &udp_input_runtime,
                                    frame_drop_manager &udp_frame_drop_manager, std::atomic<double> &luma_downscale,
                                    std::atomic<double> &luma_upscale);

    /**
     * @brief Destructor.
     */
    ~simple_status_reporter() = default;

    // Non-copyable
    simple_status_reporter(const simple_status_reporter &) = delete;
    simple_status_reporter &operator=(const simple_status_reporter &) = delete;

    /**
     * @brief Updates frame processing statistics.
     */
    void update_frame_processing();

    /**
     * @brief Updates encoding statistics.
     */
    void update_encoding();

    /**
     * @brief Updates statistics for frames pushed to encoder (after ROI processing).
     */
    void update_pushed_to_encoder();

    /**
     * @brief Updates output statistics.
     */
    void update_output();

    /**
     * @brief Updates output pipeline processing statistics (frames actually processed by pipeline).
     */
    void update_output_processed();

    /**
     * @brief Updates display branch processing statistics.
     */
    void update_output_display();

    /**
     * @brief Updates H.265 file branch processing statistics.
     */
    void update_output_h265();

    /**
     * @brief Updates MP4 branch processing statistics.
     */
    void update_output_mp4();

    /**
     * @brief Updates debug branch processing statistics.
     */
    void update_output_debug();

    /**
     * @brief Updates RTP branch processing statistics.
     */
    void update_output_rtp();

    /**
     * @brief Updates ROI detection statistics.
     * @param objects_detected Number of objects detected in current frame.
     * @param objects_tracked Number of objects being tracked.
     */
    void update_roi_detection(int objects_detected, int objects_tracked);

    /**
     * @brief Updates ROI filtering statistics.
     * @param objects_filtered Number of invalid ROIs filtered out.
     */
    void update_roi_filtering(int objects_filtered);

    /**
     * @brief Updates SEI injection statistics.
     * @param sei_count Number of SEI messages injected.
     */
    void update_sei_injection(int sei_count);

    /**
     * @brief Updates UDP decoded frame statistics (frames successfully decoded from UDP stream).
     */
    void update_udp_decoded();

    /**
     * @brief Reports a processing error.
     */
    void report_error();

    /**
     * @brief Reports a significant event.
     * @param event_description Description of the event.
     */
    void report_event(const std::string &event_description) const;

    /**
     * @brief Reports pipeline status changes.
     * @param pipeline_name Name of the pipeline.
     * @param status New status of the pipeline.
     */
    void report_pipeline_status(const std::string &pipeline_name, const std::string &status) const;

    /**
     * @brief Builds and returns the status report string (for testing and output).
     * @return The full status report text.
     */
    [[nodiscard]] std::string build_status_report_string() const;

    /**
     * @brief Generates and outputs a status report.
     */
    void generate_status_report() const;
    /**
     * @brief Starts periodic status reporting.
     * @param interval_seconds Interval between reports in seconds.
     */
    void start_periodic_reporting(int interval_seconds = 5);

    /**
     * @brief Stops periodic status reporting.
     */
    void stop_periodic_reporting();

    /**
     * @brief True while periodic status reporting is armed (GstTask driver polls this).
     */
    [[nodiscard]] bool is_periodic_reporting_active() const { return periodic_reporting_active.load(); }

    [[nodiscard]] int get_reporting_interval_seconds() const { return reporting_interval_seconds.load(); }

private:
    const encoder_config &params;
    bypass_state &bypass;
    memory_policy_runtime &memory_runtime;
    metadata_contract_runtime &metadata_runtime;
    sf::pipeline_fps_monitor &fps_monitor;
    std::atomic_bool &stop_processing;
    udp_input_runtime_state &udp_input_runtime;
    frame_drop_manager &udp_frame_drop_manager;
    std::atomic<double> &luma_downscale;
    std::atomic<double> &luma_upscale;
    std::atomic<int> frames_processed{0};
    std::atomic<int> frames_pushed_to_encoder{0}; // Frames successfully pushed to encoding pipeline
    std::atomic<int> frames_encoded{0};
    std::atomic<int> frames_output{0};
    std::atomic<int> udp_frames_decoded{0}; // Frames successfully decoded from UDP stream
    std::atomic<int> frames_output_processed{0};
    std::atomic<int> frames_output_display{0};
    std::atomic<int> frames_output_h265{0};
    std::atomic<int> frames_output_mp4{0};
    std::atomic<int> frames_output_debug{0};
    std::atomic<int> frames_output_rtp{0};
    std::atomic<int> roi_objects_detected{0};
    std::atomic<int> roi_objects_tracked{0};
    std::atomic<int> roi_objects_filtered{0};
    std::atomic<int> sei_messages_injected{0};
    std::atomic<int> processing_errors{0};
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> periodic_reporting_active{false};
    std::atomic<int> reporting_interval_seconds{5};

    /**
     * @brief Outputs a status message.
     * @param level Log level enum value.
     * @param message Status message.
     */
    static void output_status(sf::log_level level, const std::string &message);
};

/**
 * @brief Initializes the GStreamer debug category for status reporting.
 */
void init_simple_status_reporter_debug_category();
