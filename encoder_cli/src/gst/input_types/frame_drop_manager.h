// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <chrono>
#include <gst/gst.h>
#include <mutex>

class input_ingress_facade;

/**
 * @file frame_drop_manager.height
 * @brief Adaptive frame dropping manager for UDP input pipelines.
 */

/**
 * @brief Initializes the frame-drop-manager debug category.
 */
void init_frame_drop_manager_debug_category();

/**
 * @brief Manages adaptive frame dropping based on output pipeline FPS.
 *
 * This manager monitors FPS from all active output pipelines (display, RTP, WebRTC)
 * and determines when to drop frames to prevent pipeline overload.
 * All frames at decoder output are complete decoded frames and can be evaluated for dropping.
 */
struct frame_drop_manager {
    /**
     * @brief Default constructor.
     */
    frame_drop_manager();

    /**
     * @brief Determines if a frame should be dropped based on current pipeline FPS.
     *
     * @param buffer The GStreamer buffer containing the frame.
     * @param data The narrow input/ingress view containing FPS monitor and configuration.
     * @return true if the frame should be dropped, false otherwise.
     */
    [[nodiscard]] bool should_drop_frame(GstBuffer *buffer, const input_ingress_facade &data);

    /**
     * @brief Gets frame drop statistics.
     *
     * @return A struct containing current statistics.
     */
    struct statistics {
        guint64 total_frames_seen = 0;
        guint64 frames_dropped = 0;
        double smoothed_fps = 0.0;
        double drop_probability = 0.0;
    };

    [[nodiscard]] statistics get_statistics() const;

    /**
     * @brief Resets adaptive drop state and diagnostic counters for a new processing cycle.
     */
    void reset_for_processing_cycle();

private:
    /**
     * @brief Internal state structure for adaptive frame dropping.
     */
    struct frame_drop_state {
        double smoothed_fps = 0.0;
        double drop_probability = 0.0;
        std::chrono::steady_clock::time_point last_update;
        guint64 total_frames_seen = 0;
        guint64 frames_dropped = 0;
        guint64 deterministic_counter = 0; // For deterministic dropping

        frame_drop_state() : last_update(std::chrono::steady_clock::now()) {}
    };

    /**
     * @brief Gets the minimum FPS from all active pipelines.
     *
     * @param data The narrow input/ingress view containing the FPS monitor.
     * @return Minimum FPS, or 0.0 if no active pipelines.
     */
    [[nodiscard]] double get_minimum_pipeline_fps(const input_ingress_facade &data) const;

    /**
     * @brief Updates smoothed FPS using exponential moving average.
     *
     * @param current_fps Current FPS measurement.
     * @param smoothing_factor Smoothing factor from configuration.
     */
    void update_smoothed_fps(double current_fps, double smoothing_factor);

    /**
     * @brief Calculates drop probability based on FPS deficit.
     *
     * @param target_fps Target FPS from configuration.
     */
    void calculate_drop_probability(double target_fps);

    /**
     * @brief Logs frame drop statistics periodically.
     *
     * @param min_fps Minimum pipeline FPS.
     * @param target_fps Target FPS.
     */
    void log_statistics(double min_fps, double target_fps) const;

    /**
     * @brief Frame drop state (protected by mutex).
     */
    frame_drop_state state;

    /**
     * @brief Mutex for thread-safe access to state.
     */
    mutable std::mutex state_mutex;

    /**
     * @brief Counts should_drop_frame invocations (per-instance diagnostics).
     */
    std::atomic<guint64> should_drop_call_count{0};

    /**
     * @brief Counts frame evaluations for throttled logging.
     */
    std::atomic<guint64> frame_eval_count{0};

    /**
     * @brief Frame log interval for statistics logging.
     */
    static constexpr guint64 frame_log_interval = 100;
};
