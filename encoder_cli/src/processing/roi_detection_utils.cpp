// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file roi_detection_utils.cpp
 * @brief Implements utility functions for ROI detection processing.
 */

#include "roi_detection_utils.h"
#include <algorithm>

namespace {
    // Implementation details
}

/**
 * @brief Clamps a rectangle to the frame bounds.
 * @param rect Input rectangle.
 * @param frame_width Frame width in pixels.
 * @param frame_height Frame height in pixels.
 * @return Clipped rectangle within frame bounds.
 */
sf::rectangle clip_rectangle_to_frame(const sf::rectangle &rect, const int frame_width, const int frame_height) {
    sf::rectangle clipped = rect;

    // Ensure coordinates are within bounds
    clipped.left = std::max(0, std::min(clipped.left, frame_width - 1));
    clipped.top = std::max(0, std::min(clipped.top, frame_height - 1));

    // Ensure width and height don't exceed frame boundaries
    clipped.width = std::min(clipped.width, frame_width - clipped.left);
    clipped.height = std::min(clipped.height, frame_height - clipped.top);

    // Ensure width and height are positive
    clipped.width = std::max(0, clipped.width);
    clipped.height = std::max(0, clipped.height);

    return clipped;
}

/**
 * @brief Checks if a rectangle is within frame bounds and non-empty.
 * @param rect Input rectangle.
 * @param frame_width Frame width in pixels.
 * @param frame_height Frame height in pixels.
 * @return True if rectangle is valid.
 */
bool is_valid_rectangle(const sf::rectangle &rect, const int frame_width, const int frame_height) {
    return rect.width > 0 && rect.height > 0 && rect.left >= 0 && rect.top >= 0 && rect.left < frame_width &&
           rect.top < frame_height;
}
