// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <utility>
#include <vector>
#include "../../libvcai/include/rectangle.h"

/**
 * @file ctu_alignment.height
 * @brief Pure, stateless CTU alignment utility functions shared by all packers.
 *
 * These functions implement HEVC CTU (Coding Tree Unit) grid alignment. All ROI
 * placements in the packed frame happen on a grid of CTU cells (default 64×64 luma
 * samples). This avoids ROI boundaries splitting a CTU and improves HEVC compression
 * efficiency by preserving motion-compensation-friendly pixel correspondence across
 * successive packed frames.
 */

/// Default CTU size for HEVC (64×64 luma samples).
constexpr int default_ctu_size = 64;

/**
 * @brief Round a value down to the nearest multiple of alignment.
 * @param value Non-negative integer value to align.
 * @param alignment Positive alignment value (must be a power of 2).
 * @return Largest multiple of alignment that is <= value.
 *
 * Examples: align_down(65, 64) == 64, align_down(0, 64) == 0, align_down(128, 64) == 128.
 */
[[nodiscard]] constexpr int align_down(const int value, const int alignment) { return (value / alignment) * alignment; }

/**
 * @brief Round a value up to the nearest multiple of alignment.
 * @param value Non-negative integer value to align.
 * @param alignment Positive alignment value (must be a power of 2).
 * @return Smallest multiple of alignment that is >= value.
 *
 * Examples: align_up(1, 64) == 64, align_up(64, 64) == 64, align_up(65, 64) == 128.
 */
[[nodiscard]] constexpr int align_up(const int value, const int alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

/**
 * @brief Compute the smallest CTU-aligned rectangle that fully contains an ROI.
 * @param roi The source-space ROI bounding box.
 * @param ctu_size CTU cell size in pixels (default 64).
 * @return A CTU-aligned rectangle where x, y are multiples of ctu_size and
 *         w, h are multiples of ctu_size, fully enclosing the input ROI.
 *         Returns an empty rectangle if the input ROI has zero width or height.
 */
[[nodiscard]] sf::rectangle compute_ctu_rectangle(const sf::rectangle &roi, int ctu_size);

/**
 * @brief Center an ROI within its CTU rectangle.
 *
 * Returns a rectangle with the ROI's original dimensions positioned so that it is
 * horizontally and vertically centered within the CTU rectangle.
 *
 * @param roi The ROI dimensions (only w, h are used; x, y are ignored).
 * @param ctu_rect The CTU-aligned rectangle to center within.
 * @return Rectangle with position set to the centered location and original ROI dimensions.
 */
[[nodiscard]] sf::rectangle center_roi_in_ctu_rect(const sf::rectangle &roi, const sf::rectangle &ctu_rect);

/**
 * @brief Compute the number of CTU columns and rows a CTU rectangle occupies.
 * @param ctu_rect A CTU-aligned rectangle.
 * @param ctu_size CTU cell size in pixels.
 * @return Pair of (columns, rows).
 */
[[nodiscard]] std::pair<int, int> compute_ctu_grid_dimensions(const sf::rectangle &ctu_rect, int ctu_size);

/**
 * @brief Check if two CTU-aligned rectangles share any CTU cell.
 *
 * Both inputs must be CTU-aligned (positions and sizes are multiples of ctu_size).
 * Since both are CTU-aligned, this is equivalent to a standard rectangle overlap check.
 *
 * @param first First CTU-aligned rectangle.
 * @param second Second CTU-aligned rectangle.
 * @return true if the rectangles overlap (share at least one CTU cell).
 */
[[nodiscard]] bool ctu_rectangles_overlap(const sf::rectangle &first, const sf::rectangle &second);

/**
 * @brief Find a free position on the CTU grid for a CTU rectangle of given size.
 *
 * Scans positions (col * ctu_size, row * ctu_size) in row-major order starting from
 * (0,0). A position is valid when:
 * - The CTU rectangle fits within the frame (x + w <= max_width, y + h <= max_height).
 * - No cell of the new rectangle overlaps with any occupied CTU rectangle.
 *
 * @param ctu_rect_w Width of the CTU rectangle to place (must be a multiple of ctu_size).
 * @param ctu_rect_h Height of the CTU rectangle to place (must be a multiple of ctu_size).
 * @param max_width Frame width limit.
 * @param max_height Frame height limit.
 * @param ctu_size CTU cell size in pixels.
 * @param occupied_ctu_rects Currently occupied CTU rectangles.
 * @return Top-left position (x, y) of the CTU rectangle if found, nullopt otherwise.
 */
[[nodiscard]] std::optional<sf::rectangle> find_ctu_grid_position(int ctu_rect_w, int ctu_rect_h, int max_width,
                                                                  int max_height, int ctu_size,
                                                                  const std::vector<sf::rectangle> &occupied_ctu_rects);
