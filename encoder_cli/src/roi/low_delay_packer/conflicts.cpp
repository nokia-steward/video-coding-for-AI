// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "conflicts.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../../libvcai/include/rectangle.h"
#include "positioning.h"
#include "utils.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <vector>

/**
 * @file conflicts.cpp
 * @brief Implementation of conflict detection and resolution for low delay packer.
 */

GST_DEBUG_CATEGORY_EXTERN(low_delay_packer_debug);

std::vector<size_t> find_conflicting_rois(const sf::rectangle &desired_rect,
                                          const std::vector<sf::rectangle> &occupied_positions,
                                          const std::vector<size_t> &occupied_indices) {
    std::vector<std::pair<size_t, int>> conflicts_with_weights;

    for (size_t i = 0; i < occupied_positions.size(); ++i) {
        if (rectangles_overlap(desired_rect, occupied_positions[i])) {
            const int weight = get_rectangle_weight(occupied_positions[i]);
            conflicts_with_weights.emplace_back(occupied_indices[i], weight);
        }
    }

    std::ranges::sort(conflicts_with_weights, [](const auto &a, const auto &b) { return a.second < b.second; });

    std::vector<size_t> result;
    result.reserve(conflicts_with_weights.size());
    for (const auto &index: conflicts_with_weights | std::views::keys) {
        result.push_back(index);
    }

    return result;
}

std::optional<sf::rectangle> relocate_conflicting_roi(const sf::rectangle &conflicting_roi,
                                                      const sf::rectangle &grown_roi_rect,
                                                      const sf::rectangle &grown_roi_previous_pos, const int max_width,
                                                      const int max_height,
                                                      const std::vector<sf::rectangle> &occupied_positions) {
    const int right_x = grown_roi_previous_pos.left + grown_roi_rect.width;
    const int right_y = grown_roi_previous_pos.top;
    if (can_fit_at_position(conflicting_roi, right_x, right_y, max_width, max_height, occupied_positions)) {
        return sf::rectangle(right_x, right_y, conflicting_roi.width, conflicting_roi.height);
    }

    const int below_x = grown_roi_previous_pos.left;
    const int below_y = grown_roi_previous_pos.top + grown_roi_rect.height;
    if (can_fit_at_position(conflicting_roi, below_x, below_y, max_width, max_height, occupied_positions)) {
        return sf::rectangle(below_x, below_y, conflicting_roi.width, conflicting_roi.height);
    }

    if (const auto pos =
                find_position_near_edges_lower_right(conflicting_roi, max_width, max_height, occupied_positions)) {
        return pos;
    }

    if (const auto pos = find_best_position(conflicting_roi, max_width, max_height, 16, occupied_positions)) {
        return pos;
    }

    return std::nullopt;
}

bool resolve_conflicts_for_grown_roi(const sf::rectangle &grown_roi_rect, const sf::rectangle &grown_roi_previous_pos,
                                     const sf::rectangle &desired_position, const int max_width, const int max_height,
                                     std::vector<sf::rectangle> &occupied_positions,
                                     std::vector<size_t> &occupied_indices, std::vector<sf::rectangle> &packed_rects,
                                     const int frame_number) {
    const std::vector<size_t> conflicting_indices =
            find_conflicting_rois(desired_position, occupied_positions, occupied_indices);

    if (conflicting_indices.empty()) {
        return true;
    }

    for (const size_t conflict_idx: conflicting_indices) {
        auto it = std::ranges::find(occupied_indices, conflict_idx);
        if (it == occupied_indices.end()) {
            continue;
        }

        const auto occupied_pos_idx = static_cast<size_t>(std::distance(occupied_indices.begin(), it));
        const sf::rectangle conflicting_roi = occupied_positions[occupied_pos_idx];

        if (const auto new_pos = relocate_conflicting_roi(conflicting_roi, grown_roi_rect, grown_roi_previous_pos,
                                                          max_width, max_height, occupied_positions)) {
            packed_rects[conflict_idx] = *new_pos;
            occupied_positions[occupied_pos_idx] = *new_pos;
        } else {
            sf::error(low_delay_packer_debug, "Frame %d: Failed to relocate ROI %zu to make room for grown ROI",
                      frame_number, conflict_idx);
            return false;
        }
    }

    return can_fit_at_position(grown_roi_rect, desired_position.left, desired_position.top, max_width, max_height,
                               occupied_positions);
}
