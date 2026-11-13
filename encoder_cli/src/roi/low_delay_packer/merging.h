// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file merging.height
 * @brief Declares rectangle merging algorithms for low delay packer.
 */

/**
 * @brief Calculate overlap percentage between two ROIs.
 * @param roi1 First ROI
 * @param roi2 Second ROI
 * @return Overlap percentage: overlap_area / min(roi1_area, roi2_area)
 */
[[nodiscard]] double calculate_overlap_percentage(const sf::rectangle &roi1, const sf::rectangle &roi2);

/**
 * @brief Recursively merge overlapping ROIs using union-find algorithm.
 * @param accumulated_rois Vector of accumulated ROIs with their object IDs
 * @param overlap_threshold Overlap threshold (default: 0.3 = 30%)
 * @return Vector of merged ROIs with their object IDs (merged ROIs contain sets of object IDs)
 */
[[nodiscard]] std::vector<std::pair<sf::rectangle, std::unordered_set<std::string>>>
merge_overlapping_rois_recursive(const std::vector<std::pair<sf::rectangle, std::string>> &accumulated_rois,
                                 double overlap_threshold = 0.3);
