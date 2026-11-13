// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ctu_alignment.h"
#include "../../libvcai/include/rectangle.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

/**
 * @file ctu_alignment.cpp
 * @brief Implementation of CTU alignment utility functions shared by all packers.
 */

sf::rectangle compute_ctu_rectangle(const sf::rectangle &roi, const int ctu_size) {
    if (roi.width <= 0 || roi.height <= 0) {
        return sf::rectangle(0, 0, 0, 0);
    }

    const int ctu_x_start = align_down(roi.left, ctu_size);
    const int ctu_y_start = align_down(roi.top, ctu_size);
    const int right_ctu = align_up(roi.left + roi.width, ctu_size);
    const int bottom_ctu = align_up(roi.top + roi.height, ctu_size);

    return sf::rectangle(ctu_x_start, ctu_y_start, right_ctu - ctu_x_start, bottom_ctu - ctu_y_start);
}

sf::rectangle center_roi_in_ctu_rect(const sf::rectangle &roi, const sf::rectangle &ctu_rect) {
    const int offset_x = (ctu_rect.width - roi.width) / 2;
    const int offset_y = (ctu_rect.height - roi.height) / 2;
    return sf::rectangle(ctu_rect.left + offset_x, ctu_rect.top + offset_y, roi.width, roi.height);
}

std::pair<int, int> compute_ctu_grid_dimensions(const sf::rectangle &ctu_rect, const int ctu_size) {
    const int cols = ctu_rect.width / ctu_size;
    const int rows = ctu_rect.height / ctu_size;
    return {cols, rows};
}

bool ctu_rectangles_overlap(const sf::rectangle &first, const sf::rectangle &second) {
    return !(first.left + first.width <= second.left || second.left + second.width <= first.left ||
             first.top + first.height <= second.top || second.top + second.height <= first.top);
}

std::optional<sf::rectangle> find_ctu_grid_position(const int ctu_rect_w, const int ctu_rect_h, const int max_width,
                                                    const int max_height, const int ctu_size,
                                                    const std::vector<sf::rectangle> &occupied_ctu_rects) {
    // Scan row by row in CTU-grid order, prefer positions closest to origin (top-left).
    for (int y = 0; y + ctu_rect_h <= max_height; y += ctu_size) {
        for (int x = 0; x + ctu_rect_w <= max_width; x += ctu_size) {
            const sf::rectangle candidate(x, y, ctu_rect_w, ctu_rect_h);
            const bool conflicts = std::ranges::any_of(occupied_ctu_rects, [&candidate](const auto &occupied) {
                return ctu_rectangles_overlap(candidate, occupied);
            });
            if (!conflicts) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}
