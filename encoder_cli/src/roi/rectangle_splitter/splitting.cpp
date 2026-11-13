// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "splitting.h"
#include "../../../../libvcai/include/rectangle.h"
#include "validation.h"

#include <vector>

/**
 * @file splitting.cpp
 * @brief Implementation of rectangle splitting algorithms.
 */

/**
 * @brief Splits a rectangle by removing overlapping area with another rectangle.
 * @param rect_to_split Rectangle to split.
 * @param blocking_rect Rectangle that blocks part of rect_to_split.
 * @param frame_width Frame width for boundary checking.
 * @param frame_height Frame height for boundary checking.
 * @return Vector of non-overlapping rectangles.
 */
std::vector<sf::rectangle> split_rectangle_by_overlap(const sf::rectangle &rect_to_split,
                                                      const sf::rectangle &blocking_rect, const int frame_width,
                                                      const int frame_height) {
    std::vector<sf::rectangle> result;

    // Calculate the intersection area
    const auto intersection = rect_to_split.intersection(blocking_rect);
    if (intersection.area() == 0) {
        // No overlap, return original rectangle with even dimensions
        result.push_back(ensure_even_rectangle(rect_to_split, frame_width, frame_height));
        return result;
    }

    // Create rectangles for the non-overlapping parts
    // Left part (if exists)
    if (rect_to_split.left < intersection.left) {
        const int left_width = intersection.left - rect_to_split.left;
        const sf::rectangle left_rect(rect_to_split.left, rect_to_split.top, left_width, rect_to_split.height);
        result.push_back(ensure_even_rectangle(left_rect, frame_width, frame_height));
    }

    // Right part (if exists)
    if (const int right_x = intersection.left + intersection.width;
        right_x < rect_to_split.left + rect_to_split.width) {
        const int right_width = rect_to_split.left + rect_to_split.width - right_x;
        const sf::rectangle right_rect(right_x, rect_to_split.top, right_width, rect_to_split.height);
        result.push_back(ensure_even_rectangle(right_rect, frame_width, frame_height));
    }

    // Top part (if exists)
    if (rect_to_split.top < intersection.top) {
        const int top_height = intersection.top - rect_to_split.top;
        if (const int top_width = intersection.width; top_width > 0) {
            const sf::rectangle top_rect(intersection.left, rect_to_split.top, top_width, top_height);
            result.push_back(ensure_even_rectangle(top_rect, frame_width, frame_height));
        }
    }

    // Bottom part (if exists)
    if (const int bottom_y = intersection.top + intersection.height;
        bottom_y < rect_to_split.top + rect_to_split.height) {
        const int bottom_height = rect_to_split.top + rect_to_split.height - bottom_y;
        if (const int bottom_width = intersection.width; bottom_width > 0) {
            const sf::rectangle bottom_rect(intersection.left, bottom_y, bottom_width, bottom_height);
            result.push_back(ensure_even_rectangle(bottom_rect, frame_width, frame_height));
        }
    }

    // Filter out invalid rectangles (zero or negative dimensions)
    std::erase_if(result, [](const sf::rectangle &rect) { return !rect.is_valid(); });

    return result;
}
