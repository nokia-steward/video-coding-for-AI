// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <algorithm>
#include <optional>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"
#include "../ctu_alignment.h"
#include "utils.h"

/**
 * @file positioning.height
 * @brief Declares CTU-grid position finding algorithms for random access packer.
 *
 * All position finding operates on a CTU grid. Candidate positions are multiples
 * of the CTU size, and occupancy is checked at CTU-rectangle granularity.
 * The shared find_ctu_grid_position from ctu_alignment.height is the primary placement
 * function.
 */

/**
 * @brief Check if a CTU-aligned rectangle can fit at a specific CTU-grid position without overlapping.
 * @param rect The CTU-aligned rectangle to check (w, h must be multiples of ctu_size)
 * @param x X coordinate (must be a multiple of ctu_size)
 * @param y Y coordinate (must be a multiple of ctu_size)
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param occupied_ctu_rects Currently occupied CTU rectangles
 * @return true if it can fit, false otherwise
 */
[[nodiscard]] static inline bool can_fit_ctu_rect_at_position(const sf::rectangle &rect, const int x, const int y,
                                                              const int max_width, const int max_height,
                                                              const std::vector<sf::rectangle> &occupied_ctu_rects) {
    if (x < 0 || y < 0 || x + rect.width > max_width || y + rect.height > max_height) {
        return false;
    }

    const sf::rectangle candidate(x, y, rect.width, rect.height);
    return std::ranges::all_of(occupied_ctu_rects, [&candidate](const auto &occupied) {
        return !ctu_rectangles_overlap(candidate, occupied);
    });
}
