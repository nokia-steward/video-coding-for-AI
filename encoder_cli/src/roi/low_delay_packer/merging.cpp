// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "merging.h"
#include "../../../../libvcai/include/rectangle.h"

#include <algorithm>
#include <functional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @file merging.cpp
 * @brief Implementation of rectangle merging algorithms for low delay packer.
 */

/**
 * @brief Calculates the overlap percentage between two rectangles.
 * @param roi1 First rectangle.
 * @param roi2 Second rectangle.
 * @return Overlap percentage (0.0 to 1.0).
 */
double calculate_overlap_percentage(const sf::rectangle &roi1, const sf::rectangle &roi2) {
    const int intersection_x = std::max(roi1.left, roi2.left);
    const int intersection_y = std::max(roi1.top, roi2.top);
    const int intersection_w = std::max(0, std::min(roi1.left + roi1.width, roi2.left + roi2.width) - intersection_x);
    const int intersection_h = std::max(0, std::min(roi1.top + roi1.height, roi2.top + roi2.height) - intersection_y);
    const int overlap_area = intersection_w * intersection_h;

    if (overlap_area == 0) {
        return 0.0;
    }

    const int area1 = roi1.width * roi1.height;
    const int area2 = roi2.width * roi2.height;
    const int min_area = std::min(area1, area2);

    if (min_area == 0) {
        return 0.0;
    }

    return static_cast<double>(overlap_area) / static_cast<double>(min_area);
}

namespace {
    /**
     * @brief Union operation for union-find data structure.
     * @param parent Parent array for union-find.
     * @param find Find function for union-find.
     * @param x First element index.
     * @param y Second element index.
     */
    void unite_union_find(std::vector<size_t> &parent, const std::function<size_t(size_t)> &find, const size_t x,
                          const size_t second_index) {
        const size_t root_x = find(x);
        const size_t root_second_index = find(second_index);
        if (root_x != root_second_index) {
            parent[root_second_index] = root_x;
        }
    }
} // namespace

std::vector<std::pair<sf::rectangle, std::unordered_set<std::string>>>
merge_overlapping_rois_recursive(const std::vector<std::pair<sf::rectangle, std::string>> &accumulated_rois,
                                 const double overlap_threshold) {
    if (accumulated_rois.empty()) {
        return {};
    }

    const size_t num_rois = accumulated_rois.size();
    std::vector<size_t> parent(num_rois);
    for (size_t i = 0; i < num_rois; ++i) {
        parent[i] = i;
    }

    std::function<size_t(size_t)> find;
    find = [&parent, &find](const size_t x) -> size_t {
        if (parent[x] != x) {
            parent[x] = find(parent[x]);
        }
        return parent[x];
    };

    for (size_t i = 0; i < num_rois; ++i) {
        for (size_t j = i + 1; j < num_rois; ++j) {
            const double overlap = calculate_overlap_percentage(accumulated_rois[i].first, accumulated_rois[j].first);
            if (overlap >= overlap_threshold) {
                unite_union_find(parent, find, i, j);
            }
        }
    }

    std::unordered_map<size_t, std::vector<size_t>> groups;
    for (size_t i = 0; i < num_rois; ++i) {
        const size_t root = find(i);
        groups[root].push_back(i);
    }

    std::vector<std::pair<sf::rectangle, std::unordered_set<std::string>>> merged_rois;
    merged_rois.reserve(groups.size());

    for (const auto &indices: groups | std::views::values) {
        if (indices.empty()) {
            continue;
        }

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
