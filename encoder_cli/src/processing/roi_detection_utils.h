// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../libvcai/include/rectangle.h"

/**
 * @file roi_detection_utils.height
 * @brief Utility functions for ROI detection processing (rectangle clipping and validation).
 */

/**
 * @brief Clips a rectangle to frame boundaries.
 * @param rect The rectangle to clip
 * @param frame_width The width of the frame
 * @param frame_height The height of the frame
 * @return Clipped rectangle that fits within frame boundaries
 */
[[nodiscard]] sf::rectangle clip_rectangle_to_frame(const sf::rectangle &rect, int frame_width, int frame_height);

/**
 * @brief Checks if a rectangle is valid (has positive dimensions and is within frame bounds).
 * @param rect The rectangle to check
 * @param frame_width The width of the frame
 * @param frame_height The height of the frame
 * @return true if the rectangle is valid, false otherwise
 */
[[nodiscard]] bool is_valid_rectangle(const sf::rectangle &rect, int frame_width, int frame_height);
