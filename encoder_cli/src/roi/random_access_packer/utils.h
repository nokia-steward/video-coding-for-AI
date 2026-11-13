// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file utils.height
 * @brief Declares utility functions for random access packer.
 */

/**
 * @brief Calculate the weight of a rectangle (weight = area).
 * @param rect The rectangle to calculate weight for
 * @return The weight (area) of the rectangle
 */
[[nodiscard]] inline int get_rectangle_weight(const sf::rectangle &rect) { return rect.width * rect.height; }

/**
 * @brief Generate a stable ID for a rectangle based on its properties.
 * @param index The index in the input array
 * @return Generated ID string
 */
[[nodiscard]] inline std::string generate_rectangle_id(const size_t index) { return "rect_" + std::to_string(index); }

/**
 * @brief Check if two rectangles overlap.
 * @param first_rect The first rectangle to check
 * @param second_rect The second rectangle to check
 * @return true if the rectangles overlap, false otherwise
 */
[[nodiscard]] inline bool rectangles_overlap(const sf::rectangle &first_rect, const sf::rectangle &second_rect) {
    // Two rectangles overlap if neither is completely to the left, right, above, or below the other
    return !(first_rect.left + first_rect.width <= second_rect.left ||
             second_rect.left + second_rect.width <= first_rect.left ||
             first_rect.top + first_rect.height <= second_rect.top ||
             second_rect.top + second_rect.height <= first_rect.top);
}
