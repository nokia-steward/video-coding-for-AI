// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "branch_fps_tracker.h"

#include <algorithm>
#include <map>
#include "../../libvcai/include/logging.h"

/**
 * @file branch_fps_tracker.cpp
 * @brief Implementation of FPS tracking for output branches.
 */

/// GStreamer debug category for branch FPS tracking.
GST_DEBUG_CATEGORY_STATIC(branch_fps_tracker);

void init_branch_fps_tracker_debug_category() {
    GST_DEBUG_CATEGORY_INIT(branch_fps_tracker, "branch-fps-tracker", 0, "FPS tracking for output branches");
}

namespace {
    /**
     * @brief Map of branch names to their FPS tracking data.
     */
    std::map<std::string, branch_fps_data> global_branch_fps_map;
    /**
     * @brief Protects access to the FPS tracking map.
     */
    std::mutex global_branch_fps_mutex;

    /**
     * @brief Updates FPS calculation for a branch based on its timestamp queue.
     * @param fps_data The FPS data structure to update.
     * @param current_time Current timestamp.
     */
    void update_fps_calculation(branch_fps_data &fps_data, const std::chrono::steady_clock::time_point &current_time) {
        // Remove timestamps outside the window
        const auto window_start = current_time - fps_data.window_duration;
        while (!fps_data.frame_timestamps.empty() && fps_data.frame_timestamps.front() < window_start) {
            fps_data.frame_timestamps.pop_front();
        }

        // Calculate FPS if we have enough samples
        if (static_cast<int>(fps_data.frame_timestamps.size()) >= fps_data.min_frames_for_accuracy) {
            const auto time_span = std::chrono::duration_cast<std::chrono::milliseconds>(
                    fps_data.frame_timestamps.back() - fps_data.frame_timestamps.front());
            if (time_span.count() > 0) {
                const double frame_count = static_cast<double>(fps_data.frame_timestamps.size());
                const double seconds = static_cast<double>(time_span.count()) / 1000.0;
                fps_data.current_fps = frame_count / seconds;
            } else {
                // Very short time span, use frame count directly
                fps_data.current_fps = static_cast<double>(fps_data.frame_timestamps.size());
            }
        } else {
            // Not enough samples yet
            fps_data.current_fps = 0.0;
        }
    }
} // namespace

void update_branch_fps(const std::string &branch_name) {
    const auto current_time = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(global_branch_fps_mutex);

    auto &fps_data = global_branch_fps_map[branch_name];

    // Add current timestamp
    fps_data.frame_timestamps.push_back(current_time);

    // Update FPS calculation
    update_fps_calculation(fps_data, current_time);
}

/**
 * @brief Gets current FPS estimate for a named branch.
 * @param branch_name Output branch identifier.
 * @return FPS estimate or 0.0 if no data.
 */
double get_branch_fps(const std::string &branch_name) {
    std::lock_guard<std::mutex> lock(global_branch_fps_mutex);

    const auto it = global_branch_fps_map.find(branch_name);
    if (it == global_branch_fps_map.end()) {
        return 0.0;
    }

    const auto &fps_data = it->second;
    if (static_cast<int>(fps_data.frame_timestamps.size()) < fps_data.min_frames_for_accuracy) {
        return 0.0;
    }

    return fps_data.current_fps;
}
