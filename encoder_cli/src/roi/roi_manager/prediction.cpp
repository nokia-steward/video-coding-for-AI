// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "prediction.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../../libvcai/include/rectangle.h"
#include "../../processing/roi_detection_processing.h"
#include "../roi_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @file prediction.cpp
 * @brief Implementation of ROI position prediction for missing detections.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {

    /**
     * @brief Maximum number of history entries to keep per object.
     * This prevents unbounded memory growth while keeping enough history for velocity calculations.
     */
    constexpr size_t MAX_HISTORY_SIZE = 100;

    /**
     * @brief Trims object history to prevent unbounded growth.
     * Keeps the most recent MAX_HISTORY_SIZE entries.
     * @param obj The object whose history should be trimmed.
     */
    void trim_history_if_needed(detected_object &obj) {
        if (obj.history.size() > MAX_HISTORY_SIZE) {
            // Keep only the most recent entries
            const size_t entries_to_remove = obj.history.size() - MAX_HISTORY_SIZE;
            obj.history.erase(obj.history.begin(), obj.history.begin() + static_cast<ptrdiff_t>(entries_to_remove));
        }
        // Note: We don't call reserve() here to avoid potential reallocations that could trigger
        // crashes if the heap is already corrupted. The vector will grow naturally as needed.
    }

    /**
     * @brief Calculate weighted velocity considering general direction and acceleration/deceleration.
     * @param obj The object to calculate velocity for
     * @return Pair of weighted velocity (x, y)
     */
    [[nodiscard]] std::pair<float, float> calculate_weighted_velocity(const detected_object &obj) {
        if (obj.history.size() < 2) {
            return {0.0f, 0.0f};
        }

        const auto &last = obj.history.back();
        const size_t history_size = obj.history.size();
        const size_t lookback_frames = std::min(static_cast<size_t>(5), history_size - 1);

        // Calculate recent direction vector (last 2-3 frames)
        float recent_direction_x = 0.0f;
        float recent_direction_y = 0.0f;
        if (history_size >= 3) {
            const auto &third_last = obj.history[history_size - 3];
            recent_direction_x = static_cast<float>(last.bounds.left - third_last.bounds.left);
            recent_direction_y = static_cast<float>(last.bounds.top - third_last.bounds.top);

            // Normalize direction vector
            const float direction_magnitude =
                    std::sqrt(recent_direction_x * recent_direction_x + recent_direction_y * recent_direction_y);
            if (direction_magnitude > 0.0f) {
                recent_direction_x /= direction_magnitude;
                recent_direction_y /= direction_magnitude;
            }
        }

        // Calculate weighted velocities from history
        float weighted_velocity_x = 0.0f;
        float weighted_velocity_y = 0.0f;
        float total_weight = 0.0f;

        for (size_t frame_index = 1; frame_index <= lookback_frames; ++frame_index) {
            const auto &current = obj.history[history_size - frame_index];
            const auto &previous = obj.history[history_size - frame_index - 1];

            const auto velocity_x = static_cast<float>(current.bounds.left - previous.bounds.left);
            const auto velocity_y = static_cast<float>(current.bounds.top - previous.bounds.top);

            // Calculate weight based on recency and direction alignment
            const auto recency_weight = static_cast<float>(frame_index); // More recent = higher weight
            float direction_weight = 1.0f;

            // If we have a recent direction, weight movements that align with it
            if (history_size >= 3 && (recent_direction_x != 0.0f || recent_direction_y != 0.0f)) {
                if (const auto velocity_magnitude = std::sqrt(velocity_x * velocity_x + velocity_y * velocity_y);
                    velocity_magnitude > 0.0f) {
                    const auto normalized_vel_x = velocity_x / velocity_magnitude;
                    const auto normalized_vel_y = velocity_y / velocity_magnitude;

                    // Dot product to measure direction alignment
                    const float alignment =
                            normalized_vel_x * recent_direction_x + normalized_vel_y * recent_direction_y;
                    direction_weight = std::max(0.1f, alignment); // Minimum weight of 0.1 for opposite directions
                }
            }

            const auto combined_weight = recency_weight * direction_weight;

            weighted_velocity_x += velocity_x * combined_weight;
            weighted_velocity_y += velocity_y * combined_weight;
            total_weight += combined_weight;
        }

        // Normalize by total weight
        if (total_weight > 0.0f) {
            weighted_velocity_x /= total_weight;
            weighted_velocity_y /= total_weight;
        }

        // Apply acceleration/deceleration based on recent velocity changes
        if (history_size >= 3) {
            const auto &second_last = obj.history[history_size - 2];
            const auto &third_last = obj.history[history_size - 3];

            const auto recent_velocity_x = static_cast<float>(last.bounds.left - second_last.bounds.left);
            const auto recent_velocity_y = static_cast<float>(last.bounds.top - second_last.bounds.top);
            const auto older_velocity_x = static_cast<float>(second_last.bounds.left - third_last.bounds.left);
            const auto older_velocity_y = static_cast<float>(second_last.bounds.top - third_last.bounds.top);

            // Calculate acceleration
            const auto acceleration_x = recent_velocity_x - older_velocity_x;
            const auto acceleration_y = recent_velocity_y - older_velocity_y;

            // Apply acceleration to prediction (with damping factor)
            constexpr float acceleration_factor = 0.3f; // Damping factor to prevent over-prediction
            weighted_velocity_x += acceleration_x * acceleration_factor;
            weighted_velocity_y += acceleration_y * acceleration_factor;
        }

        return {weighted_velocity_x, weighted_velocity_y};
    }

    /**
     * @brief Apply velocity-based expansion to predicted bounds.
     * @param predicted_bounds The bounds to expand
     * @param velocity_x X component of velocity
     * @param velocity_y Y component of velocity
     * @param frame_width Frame width for boundary checking
     * @param frame_height Frame height for boundary checking
     */
    void apply_velocity_expansion(sf::rectangle &predicted_bounds, const float velocity_x, const float velocity_y,
                                  const int frame_width, const int frame_height) {
        // Calculate expansion based on velocity (2:1 scale, max 5 pixels per frame)
        const float velocity_magnitude = std::sqrt(velocity_x * velocity_x + velocity_y * velocity_y);
        const float expansion_factor = std::min(velocity_magnitude / 2.0f, 5.0f); // 2:1 scale, max 5 pixels

        if (expansion_factor > 0.0f && (velocity_x != 0.0f || velocity_y != 0.0f)) {
            // Normalize velocity to get direction
            const float normalized_vel_x = velocity_x / velocity_magnitude;
            const float normalized_vel_y = velocity_y / velocity_magnitude;

            // Calculate expansion in each direction
            const float expansion_x = expansion_factor * std::abs(normalized_vel_x);
            const float expansion_y = expansion_factor * std::abs(normalized_vel_y);

            // Calculate new dimensions
            const int new_width = static_cast<int>(static_cast<float>(predicted_bounds.width) + expansion_x);
            const int new_height = static_cast<int>(static_cast<float>(predicted_bounds.height) + expansion_y);

            // Calculate new position to maintain center (allowing shift)
            const int center_x = predicted_bounds.left + predicted_bounds.width / 2;
            const int center_y = predicted_bounds.top + predicted_bounds.height / 2;

            int new_x = center_x - new_width / 2;
            int new_y = center_y - new_height / 2;

            // Check frame boundaries and adjust if necessary
            if (new_x < 0) {
                new_x = 0;
            }
            if (new_y < 0) {
                new_y = 0;
            }
            if (new_x + new_width > frame_width) {
                new_x = frame_width - new_width;
            }
            if (new_y + new_height > frame_height) {
                new_y = frame_height - new_height;
            }

            // Only apply expansion if it doesn't exceed frame boundaries
            if (new_x >= 0 && new_y >= 0 && new_x + new_width <= frame_width && new_y + new_height <= frame_height) {
                predicted_bounds.left = new_x;
                predicted_bounds.top = new_y;
                predicted_bounds.width = new_width;
                predicted_bounds.height = new_height;
            }
            // If expansion would exceed boundaries, keep original bounds (stop expanding completely)
        }
    }

} // namespace

void predict_object_position(detected_object &obj, const uint32_t frame_number, const int frame_width,
                             const int frame_height) {
    if (obj.history.size() < 2) {
        // Not enough data to predict; use last known position
        if (!obj.history.empty()) {
            auto last_data = obj.history.back();
            const auto old_bounds = last_data.bounds;
            // Trim history before adding new entry to prevent large reallocations
            trim_history_if_needed(obj);

            last_data.frame_number = frame_number;
            last_data.confidence = -1.0f;
            last_data.color = sf::yuv_color::red;
            obj.history.push_back(last_data);
            sf::info(roi_detection_processing,
                     "[Frame %u] Object %u: PREDICTED (insufficient history, using last position) - "
                     "bbox=(%d,%d,%d,%d) -> (%d,%d,%d,%d), color=RED",
                     frame_number, obj.object_id, old_bounds.left, old_bounds.top, old_bounds.width, old_bounds.height,
                     last_data.bounds.left, last_data.bounds.top, last_data.bounds.width, last_data.bounds.height);
        } else {
            sf::warning(roi_detection_processing, "[Frame %u] Object %u: Cannot predict - no history available",
                        frame_number, obj.object_id);
        }
        return;
    }

    const auto &last = obj.history.back();

    // Calculate weighted velocity
    const auto [weighted_velocity_x, weighted_velocity_y] = calculate_weighted_velocity(obj);

    // Create predicted bounds using calculated velocity for position movement and size expansion
    sf::rectangle predicted_bounds = last.bounds;
    const auto old_bounds = predicted_bounds;

    // Apply velocity to move the predicted position
    predicted_bounds.left += static_cast<int>(weighted_velocity_x);
    predicted_bounds.top += static_cast<int>(weighted_velocity_y);

    // Clip to frame boundaries after movement
    if (predicted_bounds.left < 0) {
        predicted_bounds.left = 0;
    }
    if (predicted_bounds.top < 0) {
        predicted_bounds.top = 0;
    }
    if (predicted_bounds.left + predicted_bounds.width > frame_width) {
        predicted_bounds.left = frame_width - predicted_bounds.width;
    }
    if (predicted_bounds.top + predicted_bounds.height > frame_height) {
        predicted_bounds.top = frame_height - predicted_bounds.height;
    }

    // Apply velocity-based expansion
    apply_velocity_expansion(predicted_bounds, weighted_velocity_x, weighted_velocity_y, frame_width, frame_height);

    // Trim history before adding new entry to prevent large reallocations
    trim_history_if_needed(obj);

    // Create predicted object data
    const object_data predicted_data(predicted_bounds, -1.0f, frame_number, sf::yuv_color::red);
    obj.history.push_back(predicted_data);
    sf::info(roi_detection_processing,
             "[Frame %u] Object %u: PREDICTED (velocity-based) - "
             "bbox=(%d,%d,%d,%d) -> (%d,%d,%d,%d), velocity=(%.2f,%.2f), color=RED",
             frame_number, obj.object_id, old_bounds.left, old_bounds.top, old_bounds.width, old_bounds.height,
             predicted_bounds.left, predicted_bounds.top, predicted_bounds.width, predicted_bounds.height,
             static_cast<double>(weighted_velocity_x), static_cast<double>(weighted_velocity_y));
}
