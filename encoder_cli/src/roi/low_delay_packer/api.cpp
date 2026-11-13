// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../../libvcai/include/logging.h"
#include "../low_delay_packer.h"
#include "../packer_common.h"
#include "core.h"
#include "ctu_alignment.h"
#include "history.h"
#include "merging.h"
#include "utils.h"

#include <algorithm>
#include <gst/gst.h>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @file api.cpp
 * @brief Implementation of low delay packer API functions.
 */

GST_DEBUG_CATEGORY_EXTERN(low_delay_packer_debug);

low_delay_packer::low_delay_packer(const int max_width_param, const int max_height_param, const int ctu_size_param,
                                   const int num_frames_param) :
    max_width(max_width_param),
    max_height(max_height_param), ctu_size(ctu_size_param), num_frames(num_frames_param), pack_call_counter(0),
    position_memory(), slot_reservations(), roi_histories(), total_stable_count(0), total_repacked_count(0),
    compaction_wait_counter(0) {

    sf::info(low_delay_packer_debug, "Low Delay Packer initialized: %dx%d area, ctu_size=%d, num_frames=%d", max_width,
             max_height, ctu_size, num_frames);
}

std::optional<pack_result> low_delay_packer::get_packed_rectangles(const std::vector<sf::rectangle> &input_rectangles,
                                                                   const int frame_number) {
    if (input_rectangles.empty()) {
        return pack_result{{}, {}, {0, 0, 0, 0}, {}};
    }

    std::vector<stable_rectangle> stable_rects;
    stable_rects.reserve(input_rectangles.size());

    for (size_t i = 0; i < input_rectangles.size(); ++i) {
        const std::string id = generate_rectangle_id(i);
        stable_rects.emplace_back(input_rectangles[i], id, id);
    }

    const auto stable_result = ::pack_rectangles(stable_rects, frame_number, max_width, max_height, ctu_size,
                                                 pack_call_counter, position_memory, slot_reservations,
                                                 compaction_wait_counter, total_stable_count, total_repacked_count);
    if (!stable_result) {
        return std::nullopt;
    }

    std::vector<size_t> mapping;
    mapping.reserve(input_rectangles.size());
    for (size_t i = 0; i < input_rectangles.size(); ++i) {
        mapping.push_back(i);
    }

    return pack_result{input_rectangles, stable_result->packed_rectangles, stable_result->result_size, mapping};
}

std::optional<pack_result> low_delay_packer::get_packed_rectangles_with_ids(
        const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids, const int frame_number) {

    if (rectangles_with_ids.empty()) {
        return pack_result{{}, {}, {0, 0, 0, 0}, {}};
    }

    for (const auto &[rect, id]: rectangles_with_ids) {
        auto &history = roi_histories[id];
        if (history.object_id.empty()) {
            history.object_id = id;
        }
        history.frames.push_back(rect);
        while (history.frames.size() > static_cast<size_t>(num_frames)) {
            history.frames.pop_front();
        }
    }

    std::vector<std::pair<sf::rectangle, std::string>> accumulated_rois;
    accumulated_rois.reserve(rectangles_with_ids.size());

    for (const auto &[rect, id]: rectangles_with_ids) {
        const auto it = roi_histories.find(id);
        if (it != roi_histories.end() && !it->second.frames.empty()) {
            const auto &frames = it->second.frames;
            sf::rectangle accumulated = frames[0];
            for (size_t i = 1; i < frames.size(); ++i) {
                accumulated.expand_with(frames[i]);
            }
            accumulated_rois.emplace_back(accumulated, id);
        } else {
            accumulated_rois.emplace_back(rect, id);
        }
    }

    const auto merged_rois = merge_overlapping_rois_recursive(accumulated_rois, 0.3);

    std::vector<stable_rectangle> stable_rects;
    stable_rects.reserve(merged_rois.size());
    std::vector<size_t> original_to_packed_mapping;
    original_to_packed_mapping.reserve(rectangles_with_ids.size());

    for (const auto &orig_id: rectangles_with_ids | std::views::values) {
        size_t merged_idx = 0;
        bool found = false;
        for (size_t i = 0; i < merged_rois.size(); ++i) {
            if (const auto &[merged_rect, object_ids] = merged_rois[i];
                std::ranges::find(object_ids, orig_id) != object_ids.end()) {
                merged_idx = i;
                found = true;
                break;
            }
        }
        original_to_packed_mapping.push_back(found ? merged_idx : 0);
    }

    for (const auto &[merged_rect, object_ids]: merged_rois) {
        std::vector<std::string> sorted_ids(object_ids.begin(), object_ids.end());
        std::ranges::sort(sorted_ids);
        std::string composite_id = "merged_";
        for (size_t i = 0; i < sorted_ids.size(); ++i) {
            if (i > 0) {
                composite_id += "_";
            }
            composite_id += sorted_ids[i];
        }
        const std::string representative_id = sorted_ids.empty() ? composite_id : sorted_ids.front();
        stable_rects.emplace_back(merged_rect, composite_id, representative_id);
    }

    const auto stable_result = ::pack_rectangles(stable_rects, frame_number, max_width, max_height, ctu_size,
                                                 pack_call_counter, position_memory, slot_reservations,
                                                 compaction_wait_counter, total_stable_count, total_repacked_count);
    if (!stable_result) {
        return std::nullopt;
    }

    // Use accumulated_rois rectangles (before merging) for sorted_source_rectangles, matching random_access_packer
    // behavior Note: sorted_source_rectangles should contain the original accumulated ROIs (before merging)
    std::vector<sf::rectangle> input_rects;
    input_rects.reserve(accumulated_rois.size());
    for (const auto &rect: accumulated_rois | std::views::keys) {
        input_rects.push_back(rect);
    }

    return pack_result{input_rects, stable_result->packed_rectangles, stable_result->result_size,
                       original_to_packed_mapping};
}

std::optional<stable_pack_result> low_delay_packer::pack_rectangles(const std::vector<stable_rectangle> &rectangles,
                                                                    const int frame_number) {
    return ::pack_rectangles(rectangles, frame_number, max_width, max_height, ctu_size, pack_call_counter,
                             position_memory, slot_reservations, compaction_wait_counter, total_stable_count,
                             total_repacked_count);
}

void low_delay_packer::clear_memory() {
    position_memory.clear();
    slot_reservations.clear();
    roi_histories.clear();
    pack_call_counter = 0;
    total_stable_count = 0;
    total_repacked_count = 0;
    compaction_wait_counter = 0;
}

/**
 * @brief Returns a snapshot of packer statistics.
 */
low_delay_packer::stats low_delay_packer::get_stats() const {
    stats stats_snapshot{};
    stats_snapshot.total_rectangles_remembered = position_memory.size();
    stats_snapshot.total_packing_calls = static_cast<size_t>(pack_call_counter);
    stats_snapshot.average_stability_ratio =
            total_stable_count + total_repacked_count > 0
                    ? static_cast<double>(total_stable_count) /
                              static_cast<double>(total_stable_count + total_repacked_count)
                    : 0.0;
    return stats_snapshot;
}

int low_delay_packer::get_weight(const sf::rectangle &rect) { return get_rectangle_weight(rect); }
