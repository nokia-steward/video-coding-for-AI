// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <string>

/**
 * @file branch_fps_tracker.height
 * @brief FPS tracking for output branches.
 */

/**
 * @brief Structure to track FPS for a single branch using a sliding window.
 */
struct branch_fps_data {
    std::deque<std::chrono::steady_clock::time_point> frame_timestamps;
    double current_fps;
    std::chrono::milliseconds window_duration;
    int min_frames_for_accuracy;

    branch_fps_data() :
        frame_timestamps(), current_fps(0.0), window_duration(1000), // 1 second window
        min_frames_for_accuracy(5) // Minimum frames for accurate FPS calculation
    {}
};

/**
 * @brief Updates FPS tracker for a specific branch with a new frame timestamp.
 * @param branch_name Name of the branch (e.g., "display", "rtp", "file", "mp4", "debug").
 */
void update_branch_fps(const std::string &branch_name);

/**
 * @brief Gets the current FPS for a specific branch.
 * @param branch_name Name of the branch.
 * @return Current FPS value, or 0.0 if not enough samples collected.
 */
[[nodiscard]] double get_branch_fps(const std::string &branch_name);

/**
 * @brief Initializes the GStreamer debug category for branch FPS tracking.
 */
void init_branch_fps_tracker_debug_category();
