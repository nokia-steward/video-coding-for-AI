// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "packing.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../../libvcai/include/rectangle.h"
#include "../ctu_alignment.h"
#include "../packer_common.h"
#include "positioning.h"

#include <algorithm>
#include <gst/gst.h>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @file packing.cpp
 * @brief Implementation of CTU-aligned two-phase packing for random access packer.
 *
 * Implements Miska's constraints:
 * 1. At most 1 ROI bounding box per CTU cell.
 * 2. First occurrence: center ROI within its CTU rectangle.
 * 3. Subsequent occurrence: preserve pixel correspondence.
 * 4. Shrinking ROI: free CTU rows/columns, don't move others.
 * 5. Growing ROI that can't fit: relocate to unoccupied area.
 */

/// GStreamer debug category for random access packing.
GST_DEBUG_CATEGORY_STATIC(random_access_packer_debug);

namespace {

    /**
     * @brief Place a single ROI as a first occurrence (constraint 2).
     *
     * Computes the CTU rectangle for the source ROI, finds a free grid position,
     * and centers the ROI within the CTU rectangle.
     *
     * @return Pair of (packed_roi_pixel_pos, placed_ctu_rect) if successful.
     */
    std::optional<std::pair<sf::rectangle, sf::rectangle>>
    place_first_occurrence(const sf::rectangle &source_roi, const int max_width, const int max_height,
                           const int ctu_size, const std::vector<sf::rectangle> &occupied_ctu_rects) {
        // Compute smallest CTU-aligned rectangle covering the source ROI.
        const sf::rectangle source_ctu_rect = compute_ctu_rectangle(source_roi, ctu_size);

        if (source_ctu_rect.width <= 0 || source_ctu_rect.height <= 0) {
            return std::nullopt;
        }

        // Find a free position on the CTU grid.
        const auto placed_ctu_opt = find_ctu_grid_position(source_ctu_rect.width, source_ctu_rect.height, max_width,
                                                           max_height, ctu_size, occupied_ctu_rects);
        if (!placed_ctu_opt) {
            return std::nullopt;
        }

        const sf::rectangle placed_ctu_rect = *placed_ctu_opt;

        // Center the ROI within the placed CTU rectangle.
        const sf::rectangle packed_roi = center_roi_in_ctu_rect(source_roi, placed_ctu_rect);

        return std::make_pair(packed_roi, placed_ctu_rect);
    }

    /**
     * @brief Attempt correspondence-preserving placement for a recurring ROI.
     *
     * Computes the source delta between the current and previous accumulated ROI,
     * applies it to the previous packed position, and checks if the resulting CTU
     * rectangle fits (handling shrink/grow per constraints 4 and 5).
     *
     * @return Pair of (packed_roi_pixel_pos, placed_ctu_rect) if correspondence
     *         placement succeeds. nullopt if the ROI must be deferred to Phase B.
     */
    std::optional<std::pair<sf::rectangle, sf::rectangle>>
    place_with_correspondence(const sf::rectangle &current_source_roi, const position_slot &prev_slot,
                              const int max_width, const int max_height, const int ctu_size,
                              const std::vector<sf::rectangle> &occupied_ctu_rects) {
        // Compute source-space delta between periods.
        const int delta_x = current_source_roi.left - prev_slot.source_roi.left;
        const int delta_y = current_source_roi.top - prev_slot.source_roi.top;

        // Compute the new packed pixel position (preserve correspondence).
        const int new_packed_x = prev_slot.packed_roi.left + delta_x;
        const int new_packed_y = prev_slot.packed_roi.top + delta_y;
        const sf::rectangle new_packed_roi(new_packed_x, new_packed_y, current_source_roi.width,
                                           current_source_roi.height);

        // Compute the new CTU rectangle for the correspondence position.
        const sf::rectangle new_ctu_rect = compute_ctu_rectangle(new_packed_roi, ctu_size);

        if (new_ctu_rect.width <= 0 || new_ctu_rect.height <= 0) {
            return std::nullopt;
        }

        // Check bounds.
        if (new_ctu_rect.left < 0 || new_ctu_rect.top < 0 || new_ctu_rect.left + new_ctu_rect.width > max_width ||
            new_ctu_rect.top + new_ctu_rect.height > max_height) {
            return std::nullopt; // Out of frame — defer to Phase B.
        }

        // Check for conflicts with other occupied CTU rectangles.
        const bool has_conflict = std::ranges::any_of(occupied_ctu_rects, [&new_ctu_rect](const auto &occupied) {
            return ctu_rectangles_overlap(new_ctu_rect, occupied);
        });

        if (has_conflict) {
            // Constraint 5: growing ROI can't fit → defer to Phase B (relocate).
            return std::nullopt;
        }

        // Success: correspondence-preserving placement.
        // Note: for shrinking ROIs (constraint 4), the new CTU rect may be smaller
        // than the previous one. The freed cells are simply not added to occupied.
        return std::make_pair(new_packed_roi, new_ctu_rect);
    }

} // namespace

std::optional<stable_pack_result>
pack_rectangles_ctu_aligned(const std::vector<stable_rectangle> &rectangles, const int max_width, const int max_height,
                            const int ctu_size, const int pack_call_counter,
                            std::unordered_map<std::string, position_slot> &position_memory) {
    if (rectangles.empty()) {
        return stable_pack_result{{}, {0, 0, 0, 0}, 0, 0};
    }

    const size_t rectangle_count = rectangles.size();
    std::vector<sf::rectangle> packed_rois(rectangle_count);
    std::vector<sf::rectangle> occupied_ctu_rects;
    occupied_ctu_rects.reserve(rectangle_count);

    // Track which rectangles have been placed.
    std::vector<bool> placed(rectangle_count, false);
    size_t stable_count = 0;
    size_t repacked_count = 0;

    // Separate recurring and new ROIs, sorted by weight (area) descending.
    std::vector<size_t> recurring_indices;
    std::vector<size_t> new_indices;
    for (size_t i = 0; i < rectangle_count; ++i) {
        if (position_memory.contains(rectangles[i].representative_id)) {
            recurring_indices.push_back(i);
        } else {
            new_indices.push_back(i);
        }
    }

    const auto weight_sort = [&rectangles](const size_t a, const size_t b) {
        return (rectangles[a].rect.width * rectangles[a].rect.height) >
               (rectangles[b].rect.width * rectangles[b].rect.height);
    };
    std::ranges::sort(recurring_indices, weight_sort);
    std::ranges::sort(new_indices, weight_sort);

    // ── Phase A: Place recurring ROIs with correspondence ──
    std::vector<size_t> deferred_to_phase_b;

    for (const size_t idx: recurring_indices) {
        const auto &stable_rect = rectangles[idx];
        const auto &prev_slot = position_memory.at(stable_rect.representative_id);

        const auto result = place_with_correspondence(stable_rect.rect, prev_slot, max_width, max_height, ctu_size,
                                                      occupied_ctu_rects);
        if (result) {
            const auto &[roi_pos, ctu_rect] = *result;
            packed_rois[idx] = roi_pos;
            occupied_ctu_rects.push_back(ctu_rect);
            placed[idx] = true;
            stable_count++;

            sf::debug(random_access_packer_debug,
                      "Phase A: ROI '%s' placed with correspondence at (%d,%d) CTU rect (%d,%d,%d,%d)",
                      stable_rect.representative_id.c_str(), roi_pos.left, roi_pos.top, ctu_rect.left, ctu_rect.top,
                      ctu_rect.width, ctu_rect.height);
        } else {
            deferred_to_phase_b.push_back(idx);
            sf::debug(random_access_packer_debug,
                      "Phase A: ROI '%s' deferred to Phase B (correspondence placement failed)",
                      stable_rect.representative_id.c_str());
        }
    }

    // ── Phase B: Place new ROIs and deferred recurring ROIs ──
    // Deferred ROIs first (they are recurring but couldn't fit → constraint 5 relocation),
    // then genuinely new ROIs. Both sorted by weight.
    std::ranges::sort(deferred_to_phase_b, weight_sort);

    std::vector<size_t> phase_b_order;
    phase_b_order.reserve(deferred_to_phase_b.size() + new_indices.size());
    phase_b_order.insert(phase_b_order.end(), deferred_to_phase_b.begin(), deferred_to_phase_b.end());
    phase_b_order.insert(phase_b_order.end(), new_indices.begin(), new_indices.end());

    for (const size_t idx: phase_b_order) {
        const auto &stable_rect = rectangles[idx];
        const auto result =
                place_first_occurrence(stable_rect.rect, max_width, max_height, ctu_size, occupied_ctu_rects);
        if (!result) {
            sf::error(random_access_packer_debug, "Phase B: Failed to find CTU-grid position for ROI '%s' (%dx%d)",
                      stable_rect.representative_id.c_str(), stable_rect.rect.width, stable_rect.rect.height);
            return std::nullopt;
        }

        const auto &[roi_pos, ctu_rect] = *result;
        packed_rois[idx] = roi_pos;
        occupied_ctu_rects.push_back(ctu_rect);
        placed[idx] = true;
        repacked_count++;

        sf::debug(random_access_packer_debug,
                  "Phase B: ROI '%s' placed as first-occurrence at (%d,%d) CTU rect (%d,%d,%d,%d)",
                  stable_rect.representative_id.c_str(), roi_pos.left, roi_pos.top, ctu_rect.left, ctu_rect.top,
                  ctu_rect.width, ctu_rect.height);
    }

    // ── Update position memory ──
    // Clear entries for ROIs no longer present, then update/add entries for all placed ROIs.
    // Prune stale entries (not seen in this pack call).
    std::erase_if(position_memory, [pack_call_counter](const auto &entry) {
        // Keep entries from this call or the immediately previous one (1 period grace).
        return entry.second.last_pack_call < pack_call_counter - 1;
    });

    for (size_t i = 0; i < rectangle_count; ++i) {
        if (!placed[i]) {
            continue;
        }
        const auto &stable_rect = rectangles[i];
        const sf::rectangle &roi_pos = packed_rois[i];

        // Find the CTU rect for this ROI (it's in occupied_ctu_rects, but we need to
        // recompute from the packed position to store accurately).
        const sf::rectangle ctu_rect = compute_ctu_rectangle(roi_pos, ctu_size);

        // Determine if this was a first-occurrence or correspondence placement.
        const bool is_first = !position_memory.contains(stable_rect.representative_id) ||
                              std::ranges::find(deferred_to_phase_b, i) != deferred_to_phase_b.end() ||
                              std::ranges::find(new_indices, i) != new_indices.end();

        position_memory[stable_rect.representative_id] =
                position_slot(ctu_rect, stable_rect.rect, pack_call_counter, stable_rect.representative_id,
                              stable_rect.rect, roi_pos, is_first);
    }

    // ── Compute result size from occupied CTU rectangles ──
    sf::rectangle result_size(0, 0, 0, 0);
    for (const auto &ctu_rect: occupied_ctu_rects) {
        result_size.expand_with(ctu_rect);
    }

    return stable_pack_result{packed_rois, result_size, stable_count, repacked_count};
}
