// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../libvcai/include/rectangle.h"
#include "../../encoder_enums.h"

/**
 * @file stabilization.height
 * @brief Provides ROI stabilization utilities for tracked object bounds.
 */

/**
 * @struct stabilization_config
 * @brief Configuration for ROI stabilization behavior.
 */
struct stabilization_config {
    bool enabled = true;
    stabilization_mode mode = stabilization_mode::hybrid;
    float alpha = 0.3f; ///< Position smoothing (0-1, higher = more responsive)
    float alpha_size = 0.2f; ///< Size smoothing (usually smoother than position)
    int min_position_delta = 3; ///< Min pixels to update position
    int min_size_delta = 2; ///< Min pixels to update size
    float velocity_adaptive_factor = 0.5f; ///< How much velocity affects alpha
};

/**
 * @struct stabilized_state
 * @brief Smoothed state tracked per object for stabilization.
 */
struct stabilized_state {
    float smoothed_x{};
    float smoothed_y{};
    float smoothed_w{};
    float smoothed_h{};
    bool initialized = false;
};

/**
 * @brief Stabilizes bounding box coordinates using EMA and threshold logic.
 * @param raw Raw bounding box from detector.
 * @param state Per-object stabilization state (updated by this call).
 * @param config Stabilization configuration.
 * @param velocity_x Estimated velocity along X (pixels per frame).
 * @param velocity_y Estimated velocity along Y (pixels per frame).
 * @return Stabilized rectangle (rounded to integer coordinates).
 */
[[nodiscard]] sf::rectangle stabilize_bounds(const sf::rectangle &raw, stabilized_state &state,
                                             const stabilization_config &config, float velocity_x, float velocity_y);
