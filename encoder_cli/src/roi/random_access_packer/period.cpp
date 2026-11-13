// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "period.h"
#include "../../../../libvcai/include/logging.h"
#include "../packer_common.h"
#include "history.h"
#include "merging.h"
#include "packing.h"

#include <gst/gst.h>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @file period.cpp
 * @brief Implementation of intra-period processing for random access packer.
 *
 * Accumulates ROIs per object across the period, merges overlapping ones,
 * and delegates to CTU-aligned two-phase packing.
 */

/// GStreamer debug category for random access packing.
GST_DEBUG_CATEGORY_STATIC(random_access_packer_debug);

std::optional<pack_result>
process_intra_period(const std::unordered_map<std::string, object_roi_history> &object_histories, const int max_width,
                     const int max_height, const int ctu_size, int &pack_call_counter,
                     std::unordered_map<std::string, position_slot> &position_memory) {
    pack_call_counter++; // Increment counter when processing a period

    // Accumulate ROIs for each object across the period.
    // Sort object IDs to ensure consistent ordering (std::unordered_map iteration order is not guaranteed)
    std::vector<std::string> sorted_object_ids;
    sorted_object_ids.reserve(object_histories.size());
    for (const auto &object_id: object_histories | std::views::keys) {
        sorted_object_ids.push_back(object_id);
    }
    std::ranges::sort(sorted_object_ids);

    std::vector<std::pair<sf::rectangle, std::string>> accumulated_rois;
    accumulated_rois.reserve(sorted_object_ids.size());

    for (const auto &object_id: sorted_object_ids) {
        if (const auto accumulated = accumulate_rois_for_object(object_histories, object_id)) {
            accumulated_rois.emplace_back(*accumulated, object_id);
        }
    }

    if (accumulated_rois.empty()) {
        return pack_result{{}, {}, {0, 0, 0, 0}, {}};
    }

    // Merge overlapping ROIs using a 30% overlap threshold.
    const auto merged_rois = merge_overlapping_rois_recursive(accumulated_rois, 0.3);

    // Convert merged ROIs to stable_rectangle format for packing.
    std::vector<stable_rectangle> stable_rects;
    stable_rects.reserve(merged_rois.size());

    // Create mapping from original ROI index to merged ROI index
    std::vector<size_t> original_to_packed_mapping;
    original_to_packed_mapping.reserve(accumulated_rois.size());

    for (const auto &orig_id: accumulated_rois | std::views::values) {
        // Find which merged ROI this original ROI belongs to
        size_t merged_idx = 0;
        bool found = false;
        for (size_t i = 0; i < merged_rois.size(); ++i) {
            const auto &[merged_rect, object_ids] = merged_rois[i];
            // Check if this original ROI's object ID is in the merged ROI's object IDs
            if (std::ranges::find(object_ids, orig_id) != object_ids.end()) {
                merged_idx = i;
                found = true;
                break;
            }
        }
        original_to_packed_mapping.push_back(found ? merged_idx : 0);
    }

    for (const auto &[merged_rect, object_ids]: merged_rois) {
        // Create a composite ID from all object IDs in the merged ROI
        std::string composite_id = "merged_";
        bool first = true;
        for (const auto &obj_id: object_ids) {
            if (!first) {
                composite_id += "_";
            }
            composite_id += obj_id;
            first = false;
        }
        stable_rects.emplace_back(merged_rect, composite_id);
    }

    // Pack merged ROIs using CTU-aligned two-phase packing with position memory.
    const auto stable_result = pack_rectangles_ctu_aligned(stable_rects, max_width, max_height, ctu_size,
                                                           pack_call_counter, position_memory);
    if (!stable_result) {
        sf::error(random_access_packer_debug, "Failed to pack merged ROIs with CTU alignment");
        return std::nullopt;
    }

    // Convert back to the expected format
    // Note: sorted_source_rectangles should contain the original accumulated ROIs (before merging)
    std::vector<sf::rectangle> input_rects;
    input_rects.reserve(accumulated_rois.size());
    for (const auto &rect: accumulated_rois | std::views::keys) {
        input_rects.push_back(rect);
    }

    return pack_result{
            input_rects, // sorted_source_rectangles (original accumulated, before merging)
            stable_result->packed_rectangles, // packed_rectangles (merged and packed)
            stable_result->result_size, // result_size (CTU-aligned bounding box)
            original_to_packed_mapping // mapping from original ROI index to packed ROI index
    };
}
