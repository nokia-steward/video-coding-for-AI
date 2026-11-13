// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <algorithm>
#include <functional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file merging.height
 * @brief Declares rectangle merging algorithms for random access packer.
 */

namespace {

    /**
     * @brief Union operation for union-find data structure.
     */
    void unite_union_find_impl(std::vector<size_t> &parent, const std::function<size_t(size_t)> &find, const size_t x,
                               const size_t second_index) {
        const size_t root_x = find(x);
        const size_t root_second_index = find(second_index);
        if (root_x != root_second_index) {
            parent[root_second_index] = root_x;
        }
    }

} // namespace

/**
 * @brief Calculate overlap percentage between two ROIs.
 * @param roi1 First ROI
 * @param roi2 Second ROI
 * @return Overlap percentage: overlap_area / min(roi1_area, roi2_area)
 */
[[nodiscard]] static inline double calculate_overlap_percentage(const sf::rectangle &roi1, const sf::rectangle &roi2) {
    // Calculate intersection area
    const int intersection_x = std::max(roi1.left, roi2.left);
    const int intersection_y = std::max(roi1.top, roi2.top);
    const int intersection_w = std::max(0, std::min(roi1.left + roi1.width, roi2.left + roi2.width) - intersection_x);
    const int intersection_h = std::max(0, std::min(roi1.top + roi1.height, roi2.top + roi2.height) - intersection_y);
    const int overlap_area = intersection_w * intersection_h;

    if (overlap_area == 0) {
        return 0.0;
    }

    // Calculate minimum area
    const int area1 = roi1.width * roi1.height;
    const int area2 = roi2.width * roi2.height;
    const int min_area = std::min(area1, area2);

    if (min_area == 0) {
        return 0.0;
    }

    return static_cast<double>(overlap_area) / static_cast<double>(min_area);
}

/**
 * @brief Recursively merge overlapping ROIs using union-find algorithm.
 * @param accumulated_rois Vector of accumulated ROIs with their object IDs
 * @param overlap_threshold Overlap threshold (default: 0.3 = 30%)
 * @return Vector of merged ROIs with their object IDs (merged ROIs contain sets of object IDs)
 */
[[nodiscard]] static inline std::vector<std::pair<sf::rectangle, std::unordered_set<std::string>>>
merge_overlapping_rois_recursive(const std::vector<std::pair<sf::rectangle, std::string>> &accumulated_rois,
                                 const double overlap_threshold = 0.3) {
    if (accumulated_rois.empty()) {
        return {};
    }

    // Union-Find data structure for recursive merging
    const size_t num_rois = accumulated_rois.size();
    std::vector<size_t> parent(num_rois);
    for (size_t i = 0; i < num_rois; ++i) {
        parent[i] = i;
    }

    // Find function for union-find (using std::function to allow recursion)
    std::function<size_t(size_t)> find;
    find = [&parent, &find](const size_t x) -> size_t {
        if (parent[x] != x) {
            parent[x] = find(parent[x]); // Path compression
        }
        return parent[x];
    };

    // Check all pairs for overlap
    for (size_t i = 0; i < num_rois; ++i) {
        for (size_t j = i + 1; j < num_rois; ++j) {
            const double overlap = calculate_overlap_percentage(accumulated_rois[i].first, accumulated_rois[j].first);
            if (overlap >= overlap_threshold) {
                unite_union_find_impl(parent, find, i, j);
            }
        }
    }

    // Group ROIs by their root (merged sets)
    std::unordered_map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < num_rois; ++i) {
        const size_t root = find(i);
        groups[root].push_back(i);
    }

    // Create merged ROIs
    std::vector<std::pair<sf::rectangle, std::unordered_set<std::string>>> merged_rois;
    merged_rois.reserve(groups.size());

    for (const auto &indices: groups | std::views::values) {
        if (indices.empty()) {
            continue;
        }

        // Compute merged bounding box (union of all ROIs in group)
        sf::rectangle merged_rect = accumulated_rois[indices[0]].first;
        std::unordered_set<std::string> object_ids;
        object_ids.insert(accumulated_rois[indices[0]].second);

        for (size_t idx = 1; idx < indices.size(); ++idx) {
            merged_rect.expand_with(accumulated_rois[indices[idx]].first);
            object_ids.insert(accumulated_rois[indices[idx]].second);
        }

        merged_rois.emplace_back(merged_rect, std::move(object_ids));
    }

    return merged_rois;
}
