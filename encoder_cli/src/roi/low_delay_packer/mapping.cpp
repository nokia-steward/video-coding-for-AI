// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "mapping.h"
#include "../../../../libvcai/include/rectangle.h"
#include "../packer_common.h"
#include "utils.h"

#include <algorithm>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @file mapping.cpp
 * @brief Implementation of position memory mapping for low delay packer.
 */

position_slot::position_slot() :
    slot_bounds(), last_roi(), last_pack_call(0), representative_id(), source_roi(), packed_roi(),
    is_first_occurrence(true) {}

position_slot::position_slot(const sf::rectangle &slot, const sf::rectangle &roi, const int call,
                             std::string representative) :
    slot_bounds(slot),
    last_roi(roi), last_pack_call(call), representative_id(std::move(representative)), source_roi(), packed_roi(),
    is_first_occurrence(true) {}

position_slot::position_slot(const sf::rectangle &slot, const sf::rectangle &roi, const int call,
                             std::string representative, const sf::rectangle &source, const sf::rectangle &packed,
                             const bool first_occurrence) :
    slot_bounds(slot),
    last_roi(roi), last_pack_call(call), representative_id(std::move(representative)), source_roi(source),
    packed_roi(packed), is_first_occurrence(first_occurrence) {}

std::pair<std::vector<size_t>, std::vector<size_t>>
get_recurring_rois_fit_status(const std::vector<stable_rectangle> &rectangles,
                              const std::unordered_map<std::string, position_slot> &position_memory,
                              const int max_width, const int max_height) {
    std::vector<size_t> recurring_can_fit;
    std::vector<size_t> recurring_cannot_fit;
    std::vector<sf::rectangle> occupied_by_recurring;

    std::vector<std::pair<size_t, int>> recurring_with_weights;
    for (size_t i = 0; i < rectangles.size(); ++i) {
        const auto &stable_rect = rectangles[i];
        if (position_memory.contains(stable_rect.representative_id)) {
            const int weight = get_rectangle_weight(stable_rect.rect);
            recurring_with_weights.emplace_back(i, weight);
        }
    }

    std::ranges::sort(recurring_with_weights, [](const auto &a, const auto &b) { return a.second > b.second; });

    for (const auto &index: recurring_with_weights | std::views::keys) {
        const auto &stable_rect = rectangles[index];
        const auto &previous_slot = position_memory.at(stable_rect.representative_id).slot_bounds;

        if (can_fit_at_position(previous_slot, previous_slot.left, previous_slot.top, max_width, max_height,
                                occupied_by_recurring)) {
            recurring_can_fit.push_back(index);
            occupied_by_recurring.emplace_back(previous_slot.left, previous_slot.top, previous_slot.width,
                                               previous_slot.height);
        } else {
            recurring_cannot_fit.push_back(index);
        }
    }

    return {recurring_can_fit, recurring_cannot_fit};
}

std::vector<size_t> get_priority_order_indices(const std::vector<stable_rectangle> &rectangles,
                                               const std::unordered_map<std::string, position_slot> &position_memory) {
    std::vector<size_t> with_previous;
    std::vector<size_t> without_previous;

    for (size_t i = 0; i < rectangles.size(); ++i) {
        if (position_memory.contains(rectangles[i].representative_id)) {
            with_previous.push_back(i);
        } else {
            without_previous.push_back(i);
        }
    }

    const auto sort_by_weight = [&rectangles](const size_t a, const size_t b) {
        return get_rectangle_weight(rectangles[a].rect) > get_rectangle_weight(rectangles[b].rect);
    };

    std::ranges::sort(with_previous, sort_by_weight);
    std::ranges::sort(without_previous, sort_by_weight);

    std::vector<size_t> result;
    result.reserve(rectangles.size());
    result.insert(result.end(), with_previous.begin(), with_previous.end());
    result.insert(result.end(), without_previous.begin(), without_previous.end());

    return result;
}
