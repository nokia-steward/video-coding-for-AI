// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>

/**
 * @file pipeline_fps_monitor.h
 * @brief Rolling window FPS monitor for named pipelines.
 */

namespace sf {
    /**
     * @brief Tracks FPS using a rolling time window with periodic updates.
     */
    struct pipeline_fps_monitor {
        /**
         * @brief Default constructor.
         */
        pipeline_fps_monitor() : pipeline_fps_map(), fps_mutex() {}

        /**
         * @brief Records a frame arrival for a pipeline.
         * @param pipeline_name Name of the pipeline ("display", "rtp", "webrtc").
         */
        void record_frame(const std::string &pipeline_name);

        /**
         * @brief Gets the current FPS value for a pipeline.
         * @param pipeline_name Name of the pipeline.
         * @return Current FPS value, or 0.0 if insufficient data.
         */
        [[nodiscard]] double get_current_fps(const std::string &pipeline_name) const;

        /**
         * @brief Checks if a pipeline has recent activity.
         * @param pipeline_name Name of the pipeline.
         * @return true if the pipeline has recent frames, false otherwise.
         */
        [[nodiscard]] bool is_pipeline_active(const std::string &pipeline_name) const;

    private:
        /**
         * @brief Structure to track rolling-window FPS for a pipeline.
         */
        struct pipeline_fps_data {
            std::deque<std::chrono::steady_clock::time_point> frame_timestamps;
            std::chrono::steady_clock::time_point last_update_time;
            double current_fps = 0.0;
        };

        mutable std::map<std::string, pipeline_fps_data> pipeline_fps_map;
        mutable std::mutex fps_mutex;

        /**
         * @brief Rolling window duration used for FPS calculation.
         */
        static constexpr std::chrono::milliseconds window_duration{1000};

        /**
         * @brief Minimum interval between FPS recalculations.
         */
        static constexpr std::chrono::milliseconds update_interval{500};

        /**
         * @brief Minimum frames needed before reporting FPS.
         */
        static constexpr int min_frames_for_display = 10;
    };
} // namespace sf
