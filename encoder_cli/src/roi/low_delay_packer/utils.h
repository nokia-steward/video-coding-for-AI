// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file utils.height
 * @brief Declares utility functions for low delay packer.
 */

/**
 * @brief Calculate the weight of a rectangle (weight = area).
 * @param rect The rectangle to calculate weight for
 * @return The weight (area) of the rectangle
 */
[[nodiscard]] int get_rectangle_weight(const sf::rectangle &rect);

/**
 * @brief Generate a stable ID for a rectangle based on its properties.
 * @param index The index in the input array
 * @return Generated ID string
 */
[[nodiscard]] std::string generate_rectangle_id(size_t index);

/**
 * @brief Check if two rectangles overlap.
 * @param first_rect The first rectangle to check
 * @param second_rect The second rectangle to check
 * @return true if the rectangles overlap, false otherwise
 */
[[nodiscard]] inline bool rectangles_overlap(const sf::rectangle &first_rect, const sf::rectangle &second_rect) {
    return !(first_rect.left + first_rect.width <= second_rect.left ||
             second_rect.left + second_rect.width <= first_rect.left ||
             first_rect.top + first_rect.height <= second_rect.top ||
             second_rect.top + second_rect.height <= first_rect.top);
}

/**
 * @brief Check if a rectangle can fit at a specific position without overlapping.
 * @param rect The rectangle to check
 * @param x X coordinate
 * @param y Y coordinate
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_positions Currently occupied positions
 * @return true if it can fit, false otherwise
 */
[[nodiscard]] bool can_fit_at_position(const sf::rectangle &rect, int x, int y, int max_width, int max_height,
                                       const std::vector<sf::rectangle> &occupied_positions);

/**
 * @brief Check if compaction is needed based on ROI density threshold.
 * @param packed_rects The packed rectangles
 * @param bounding_box The bounding box of all packed rectangles
 * @param threshold The density threshold (default: 0.75 = 75%)
 * @return true if compaction is needed (density < threshold), false otherwise
 */
[[nodiscard]] bool needs_compaction(const std::vector<sf::rectangle> &packed_rects, const sf::rectangle &bounding_box,
                                    double threshold = 0.75);
