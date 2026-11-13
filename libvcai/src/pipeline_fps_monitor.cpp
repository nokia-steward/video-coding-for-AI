// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../include/pipeline_fps_monitor.h"

#include <chrono>

namespace sf {
    void pipeline_fps_monitor::record_frame(const std::string &pipeline_name) {
        const auto current_time = std::chrono::steady_clock::now();
        const std::lock_guard lock(fps_mutex);

        auto &fps_data = pipeline_fps_map[pipeline_name];
        fps_data.frame_timestamps.push_back(current_time);

        const auto window_start = current_time - window_duration;
        while (!fps_data.frame_timestamps.empty() && fps_data.frame_timestamps.front() < window_start) {
            fps_data.frame_timestamps.pop_front();
        }

        const auto time_since_last_update = current_time - fps_data.last_update_time;
        if (fps_data.last_update_time.time_since_epoch().count() == 0 || time_since_last_update >= update_interval) {
            if (static_cast<int>(fps_data.frame_timestamps.size()) >= min_frames_for_display) {
                const auto time_span = std::chrono::duration_cast<std::chrono::milliseconds>(
                        fps_data.frame_timestamps.back() - fps_data.frame_timestamps.front());
                if (time_span.count() > 0) {
                    const double frame_count = static_cast<double>(fps_data.frame_timestamps.size());
                    const double seconds = static_cast<double>(time_span.count()) / 1000.0;
                    fps_data.current_fps = frame_count / seconds;
                } else {
                    fps_data.current_fps = static_cast<double>(fps_data.frame_timestamps.size());
                }
            }
            fps_data.last_update_time = current_time;
        }
    }

    double pipeline_fps_monitor::get_current_fps(const std::string &pipeline_name) const {
        const std::lock_guard lock(fps_mutex);

        const auto it = pipeline_fps_map.find(pipeline_name);
        if (it == pipeline_fps_map.end()) {
            return 0.0;
        }

        const auto &fps_data = it->second;
        if (static_cast<int>(fps_data.frame_timestamps.size()) < min_frames_for_display) {
            return 0.0;
        }

        return fps_data.current_fps;
    }

    bool pipeline_fps_monitor::is_pipeline_active(const std::string &pipeline_name) const {
        const std::lock_guard lock(fps_mutex);

        const auto it = pipeline_fps_map.find(pipeline_name);
        if (it == pipeline_fps_map.end()) {
            return false;
        }

        const auto &fps_data = it->second;
        if (fps_data.frame_timestamps.empty()) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto time_since_last_frame = now - fps_data.frame_timestamps.back();
        return time_since_last_frame <= (window_duration * 2);
    }
} // namespace sf
