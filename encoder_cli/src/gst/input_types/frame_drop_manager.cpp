// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "frame_drop_manager.h"

#include <algorithm>
#include <limits>

#include "../../libvcai/include/logging.h"
#include "input_ingress_facade.h"

/**
 * @file frame_drop_manager.cpp
 * @brief Implementation of adaptive frame dropping manager.
 */

/// GStreamer debug category for frame drop manager diagnostics.
GST_DEBUG_CATEGORY_STATIC(frame_drop_manager_category);

void init_frame_drop_manager_debug_category() {
    GST_DEBUG_CATEGORY_INIT(frame_drop_manager_category, "frame-drop-manager", 0,
                            "Adaptive frame dropping for UDP input");
}

frame_drop_manager::frame_drop_manager() : state(), state_mutex(), should_drop_call_count(0), frame_eval_count(0) {}

void frame_drop_manager::reset_for_processing_cycle() {
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        state = frame_drop_state{};
    }
    should_drop_call_count.store(0);
    frame_eval_count.store(0);
}

/**
 * @brief Computes the minimum FPS across active output pipelines.
 * @param data Global data with pipeline FPS monitor.
 * @return Minimum FPS or 0.0 if unavailable.
 */
double frame_drop_manager::get_minimum_pipeline_fps(const input_ingress_facade &data) const {
    double min_fps = std::numeric_limits<double>::max();
    bool has_active_pipeline = false;

    if (data.monitoring.fps_monitor.is_pipeline_active("display")) {
        const double display_fps = data.monitoring.fps_monitor.get_current_fps("display");
        if (display_fps > 0.0) {
            min_fps = std::min(min_fps, display_fps);
            has_active_pipeline = true;
        }
    }

    if (data.monitoring.fps_monitor.is_pipeline_active("rtp")) {
        const double rtp_fps = data.monitoring.fps_monitor.get_current_fps("rtp");
        if (rtp_fps > 0.0) {
            min_fps = std::min(min_fps, rtp_fps);
            has_active_pipeline = true;
        }
    }

    if (data.monitoring.fps_monitor.is_pipeline_active("webrtc")) {
        const double webrtc_fps = data.monitoring.fps_monitor.get_current_fps("webrtc");
        if (webrtc_fps > 0.0) {
            min_fps = std::min(min_fps, webrtc_fps);
            has_active_pipeline = true;
        }
    }

    // Return 0.0 if no active pipelines or invalid FPS
    if (!has_active_pipeline || min_fps == std::numeric_limits<double>::max() || min_fps <= 0.0) {
        return 0.0;
    }

    return min_fps;
}

void frame_drop_manager::update_smoothed_fps(double current_fps, double smoothing_factor) {
    if (state.smoothed_fps == 0.0) {
        // Initialize smoothed FPS on first frame
        state.smoothed_fps = current_fps;
    } else {
        state.smoothed_fps = (smoothing_factor * current_fps) + ((1.0 - smoothing_factor) * state.smoothed_fps);
    }
}

void frame_drop_manager::calculate_drop_probability(double target_fps) {
    // Minimum FPS threshold: if FPS drops below this, stop dropping to allow recovery
    constexpr double min_fps_threshold = 5.0;

    // Maximum drop probability: never drop more than 50% of frames to prevent death spiral
    constexpr double max_drop_probability = 0.5;

    if (state.smoothed_fps < target_fps && state.smoothed_fps > min_fps_threshold) {
        const double fps_deficit = target_fps - state.smoothed_fps;
        const double deficit_ratio = fps_deficit / target_fps;
        // Cap drop probability to prevent excessive dropping
        state.drop_probability = std::min(max_drop_probability, deficit_ratio);
    } else {
        // Stop dropping if FPS is at target, too low (recovery needed), or not initialized
        state.drop_probability = 0.0;
    }
}

void frame_drop_manager::log_statistics(double min_fps, double target_fps) const {
    const double drop_rate =
            state.total_frames_seen > 0
                    ? (static_cast<double>(state.frames_dropped) / static_cast<double>(state.total_frames_seen)) * 100.0
                    : 0.0;
    sf::debug(frame_drop_manager_category,
              "Frame drop stats: total=%llu, dropped=%llu (%.2f%%), "
              "min_fps=%.2f, smoothed_fps=%.2f, target_fps=%.2f, drop_prob=%.2f",
              static_cast<unsigned long long>(state.total_frames_seen),
              static_cast<unsigned long long>(state.frames_dropped), drop_rate, min_fps, state.smoothed_fps, target_fps,
              state.drop_probability);
}

/**
 * @brief Determines whether an incoming frame should be dropped.
 * @param buffer Incoming buffer (may be null).
 * @param data Global data with performance settings.
 * @return True if the frame should be dropped.
 */
bool frame_drop_manager::should_drop_frame(GstBuffer *buffer, const input_ingress_facade &data) {
    const guint64 current_call = should_drop_call_count.fetch_add(1) + 1;

    if (current_call == 1) {
        sf::info(frame_drop_manager_category,
                 "Frame drop manager first call - enable_frame_dropping=%d, target_fps=%.2f",
                 data.params.performance.enable_frame_dropping ? 1 : 0, data.params.performance.target_fps);
    }

    if (!buffer) {
        sf::debug(frame_drop_manager_category, "should_drop_frame called with null buffer");
        return false;
    }

    std::lock_guard<std::mutex> lock(state_mutex);

    // Note: All frames at decoder output are complete decoded frames.
    // Keyframe checks are not needed here since frames are already decoded.

    // Check if frame dropping is enabled
    if (!data.params.performance.enable_frame_dropping) {
        state.total_frames_seen++;
        if (state.total_frames_seen == 1) {
            sf::info(frame_drop_manager_category, "Frame drop manager active but frame dropping is disabled in config");
        }
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    state.total_frames_seen++;
    state.last_update = now;

    // Get FPS from all active pipelines
    const double min_fps = get_minimum_pipeline_fps(data);

    // Log first few frames to see what's happening
    const guint64 eval_num = frame_eval_count.fetch_add(1) + 1;
    if (eval_num <= 10 || eval_num % 50 == 0) {
        sf::info(frame_drop_manager_category, "Frame evaluation #%llu: min_fps=%.2f, total_frames_seen=%llu",
                 static_cast<unsigned long long>(eval_num), min_fps,
                 static_cast<unsigned long long>(state.total_frames_seen));
    }

    // If no active pipelines or FPS is 0, don't drop frames
    if (min_fps <= 0.0) {
        if (eval_num <= 10 || state.total_frames_seen % frame_log_interval == 0) {
            sf::info(frame_drop_manager_category,
                     "No active pipelines or FPS is 0: min_fps=%.2f, total_frames_seen=%llu", min_fps,
                     static_cast<unsigned long long>(state.total_frames_seen));
        }
        return false;
    }

    // Update smoothed FPS using exponential moving average
    const double smoothing_factor = data.params.performance.drop_smoothing_factor;
    update_smoothed_fps(min_fps, smoothing_factor);

    // Calculate drop probability if FPS is below target
    const double target_fps = data.params.performance.target_fps;
    calculate_drop_probability(target_fps);

    // Apply deterministic dropping based on probability
    bool should_drop = false;
    if (state.drop_probability > 0.0) {
        // Deterministic: drop every Nth frame based on probability
        // If drop_probability is 0.5, drop every 2nd frame
        // Minimum drop_interval of 2 ensures we never drop more than every other frame
        const double drop_interval_double = 1.0 / state.drop_probability;
        const guint64 drop_interval = std::max(static_cast<guint64>(drop_interval_double), static_cast<guint64>(2));

        state.deterministic_counter++;
        if (state.deterministic_counter >= drop_interval) {
            should_drop = true;
            state.deterministic_counter = 0;
            sf::info(frame_drop_manager_category,
                     "Deterministic drop triggered: drop_interval=%llu, counter reached threshold",
                     static_cast<unsigned long long>(drop_interval));
        }
    } else {
        // Log why drop_probability is 0 - use info level for first few and periodic
        if (eval_num <= 10 || state.total_frames_seen % frame_log_interval == 0) {
            if (state.smoothed_fps >= target_fps) {
                sf::info(frame_drop_manager_category, "No drop needed: smoothed_fps=%.2f >= target_fps=%.2f",
                         state.smoothed_fps, target_fps);
            } else if (state.smoothed_fps <= 0.0) {
                sf::info(frame_drop_manager_category, "No drop: smoothed_fps=%.2f <= 0.0 (not initialized yet)",
                         state.smoothed_fps);
            } else if (state.smoothed_fps <= 5.0) {
                sf::info(frame_drop_manager_category, "No drop: smoothed_fps=%.2f <= 5.0 (recovery mode - FPS too low)",
                         state.smoothed_fps);
            }
        }
    }

    // Log statistics periodically with detailed evaluation info
    if (state.total_frames_seen % frame_log_interval == 0) {
        log_statistics(min_fps, target_fps);
        sf::info(frame_drop_manager_category,
                 "Frame evaluation details: min_fps=%.2f, smoothed_fps=%.2f, target_fps=%.2f, "
                 "drop_prob=%.4f, deterministic_counter=%llu",
                 min_fps, state.smoothed_fps, target_fps, state.drop_probability,
                 static_cast<unsigned long long>(state.deterministic_counter));
    }

    if (should_drop) {
        state.frames_dropped++;
        sf::debug(frame_drop_manager_category, "Dropping frame: min_fps=%.2f < target_fps=%.2f, drop_prob=%.4f",
                  min_fps, target_fps, state.drop_probability);
    }

    return should_drop;
}

/**
 * @brief Returns a snapshot of frame drop statistics.
 */
frame_drop_manager::statistics frame_drop_manager::get_statistics() const {
    std::lock_guard<std::mutex> lock(state_mutex);
    statistics stats;
    stats.total_frames_seen = state.total_frames_seen;
    stats.frames_dropped = state.frames_dropped;
    stats.smoothed_fps = state.smoothed_fps;
    stats.drop_probability = state.drop_probability;
    return stats;
}
