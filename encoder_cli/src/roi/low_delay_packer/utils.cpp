// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "utils.h"
#include "../../../../libvcai/include/rectangle.h"

#include <algorithm>
#include <string>
#include <vector>

/**
 * @file utils.cpp
 * @brief Utility functions for low delay packer.
 */

/**
 * @brief Calculates the weight (area) of a rectangle.
 * @param rect Rectangle to calculate weight for.
 * @return Rectangle area (width * height).
 */
int get_rectangle_weight(const sf::rectangle &rect) { return rect.width * rect.height; }

/**
 * @brief Generates a stable rectangle identifier string.
 * @param index Rectangle index.
 * @return Identifier string.
 */
std::string generate_rectangle_id(const size_t index) { return "rect_" + std::to_string(index); }


bool can_fit_at_position(const sf::rectangle &rect, const int x, const int y, const int max_width, const int max_height,
                         const std::vector<sf::rectangle> &occupied_positions) {
    if (x < 0 || y < 0 || x + rect.width > max_width || y + rect.height > max_height) {
        return false;
    }

    const sf::rectangle test_rect(x, y, rect.width, rect.height);
    return std::ranges::all_of(occupied_positions,
                               [&test_rect](const auto &occupied) { return !rectangles_overlap(test_rect, occupied); });
}

bool needs_compaction(const std::vector<sf::rectangle> &packed_rects, const sf::rectangle &bounding_box,
                      const double threshold) {
    if (packed_rects.empty() || bounding_box.width <= 0 || bounding_box.height <= 0) {
        return false;
    }

    int total_roi_area = 0;
    for (const auto &rect: packed_rects) {
        total_roi_area += rect.width * rect.height;
    }

    const int bounding_box_area = bounding_box.width * bounding_box.height;
    if (bounding_box_area == 0) {
        return false;
    }

    const double density = static_cast<double>(total_roi_area) / static_cast<double>(bounding_box_area);
    return density < threshold;
}
