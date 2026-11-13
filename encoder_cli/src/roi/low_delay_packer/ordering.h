// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <vector>
#include "../packer_common.h"

/**
 * @file ordering.height
 * @brief Declares rectangle ordering algorithms for low delay packer.
 */

/**
 * @brief Sort rectangles by area in descending order (largest first).
 */
[[nodiscard]] std::vector<stable_rectangle> get_sorted_by_area_desc(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Sort rectangles by area in ascending order (smallest first).
 */
[[nodiscard]] std::vector<stable_rectangle> get_sorted_by_area_asc(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Sort rectangles by width in descending order (widest first).
 */
[[nodiscard]] std::vector<stable_rectangle> get_sorted_by_width_desc(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Sort rectangles by height in descending order (tallest first).
 */
[[nodiscard]] std::vector<stable_rectangle> get_sorted_by_height_desc(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Get original order indices (0, 1, 2, ...).
 */
[[nodiscard]] std::vector<size_t> get_original_order(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Get indices sorted by area in descending order (largest first).
 */
[[nodiscard]] std::vector<size_t> get_sorted_by_area_desc_indices(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Get indices sorted by area in ascending order (smallest first).
 */
[[nodiscard]] std::vector<size_t> get_sorted_by_area_asc_indices(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Get indices sorted by width in descending order (widest first).
 */
[[nodiscard]] std::vector<size_t> get_sorted_by_width_desc_indices(const std::vector<stable_rectangle> &rectangles);

/**
 * @brief Get indices sorted by height in descending order (tallest first).
 */
[[nodiscard]] std::vector<size_t> get_sorted_by_height_desc_indices(const std::vector<stable_rectangle> &rectangles);
