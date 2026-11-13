// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "packing.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../../libvcai/include/rectangle.h"
#include "ctu_alignment.h"
#include "mapping.h"
#include "ordering.h"
#include "positioning.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <gst/gst.h>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * @file packing.cpp
 * @brief CTU-aligned packing algorithm implementation for low delay packer.
 *
 * The algorithm enforces the following constraints:
 * 1. On the CTU grid, a single CTU cell is occupied by at most one ROI bounding box.
 * 2. First occurrence: the ROI is centered within its enclosing CTU rectangle.
 * 3. Subsequent occurrences: pixel correspondence is preserved — packed position shifts by
 *    the same delta as the source-space ROI position.
 * 4. Shrinking ROIs release complete CTU rows/columns without moving other ROIs.
 * 5. Growing ROIs that cannot expand in place are relocated to free CTU cells.
 */

GST_DEBUG_CATEGORY_EXTERN(low_delay_packer_debug);

namespace {
    /**
     * @brief Validates packed rectangle dimensions.
     * @param rect Rectangle to validate
     * @param original_w Original width
     * @param original_h Original height
     * @return true if valid, false otherwise
     */
    [[nodiscard]] bool validate_packed_rectangle(const sf::rectangle &rect, const int original_w,
                                                 const int original_h) {
        if (rect.width != original_w || rect.height != original_h) {
            sf::error(low_delay_packer_debug,
                      "Packing algorithm modified rectangle dimensions! Original: %dx%d, Packed: %dx%d", original_w,
                      original_h, rect.width, rect.height);
            return false;
        }
        if (rect.width <= 0 || rect.height <= 0) {
            sf::error(low_delay_packer_debug, "Packing algorithm created invalid rectangle dimensions: %dx%d",
                      rect.width, rect.height);
            return false;
        }
        return true;
    }

    /**
     * @brief Validates all packed rectangles.
     * @param packed_rects Packed rectangles
     * @return true if all valid, false otherwise
     */
    [[nodiscard]] bool validate_all_packed_rectangles(const std::vector<sf::rectangle> &packed_rects) {
        for (size_t i = 0; i < packed_rects.size(); ++i) {
            const auto &rect = packed_rects[i];
            if (rect.width <= 0 || rect.height <= 0) {
                sf::error(low_delay_packer_debug,
                          "Packing algorithm produced invalid rectangle at index %zu: %dx%d at (%d,%d)", i, rect.width,
                          rect.height, rect.left, rect.top);
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Calculates bounding box of all CTU rectangles (slot_bounds) that were placed.
     *
     * The result_size reflects the CTU-aligned bounding box so that the encoder sees
     * a region whose dimensions are multiples of the CTU size.
     *
     * @param occupied_ctu_rects All occupied CTU rectangles.
     * @return Bounding box rectangle covering all CTU rectangles.
     */
    [[nodiscard]] sf::rectangle calculate_ctu_result_size(const std::vector<sf::rectangle> &occupied_ctu_rects) {
        sf::rectangle result_size(0, 0, 0, 0);
        for (const auto &rect: occupied_ctu_rects) {
            result_size.expand_with(rect);
        }
        return result_size;
    }

    /**
     * @brief Update slot reservations — reserve CTU rectangles of recently disappeared ROIs.
     *
     * When an ROI disappears, its CTU rectangle is reserved for a number of frames to
     * prevent other ROIs from occupying those CTU cells. This improves temporal stability
     * if the ROI reappears shortly.
     */
    void update_slot_reservations(const std::unordered_set<std::string> &current_ids,
                                  const std::unordered_map<std::string, position_slot> &position_memory,
                                  std::unordered_map<std::string, slot_reservation> &slot_reservations,
                                  const int pack_call_counter) {
        const int reservation_frames = 5;
        for (auto it = slot_reservations.begin(); it != slot_reservations.end();) {
            if (current_ids.contains(it->second.owner_id)) {
                it = slot_reservations.erase(it);
                continue;
            }
            it->second.frames_remaining -= 1;
            if (it->second.frames_remaining <= 0) {
                it = slot_reservations.erase(it);
                continue;
            }
            ++it;
        }

        for (const auto &[slot_id, slot]: position_memory) {
            if (current_ids.contains(slot_id) || slot_reservations.contains(slot_id)) {
                continue;
            }
            const int frames_since_seen = pack_call_counter - slot.last_pack_call;
            if (frames_since_seen <= 0) {
                continue;
            }
            const int remaining = reservation_frames - frames_since_seen + 1;
            if (remaining <= 0) {
                continue;
            }
            slot_reservations.emplace(slot_id, slot_reservation{slot.slot_bounds, slot_id, remaining});
        }
    }

    /**
     * @brief Place a first-occurrence ROI: find CTU grid position, center the ROI within it.
     *
     * @param source_roi Source-space ROI bounding box.
     * @param ctu_size CTU cell size.
     * @param max_width Frame width limit.
     * @param max_height Frame height limit.
     * @param occupied_ctu_rects Currently occupied CTU rectangles.
     * @return Pair of (packed_roi_position, ctu_rectangle) or nullopt on failure.
     */
    [[nodiscard]] std::optional<std::pair<sf::rectangle, sf::rectangle>>
    place_first_occurrence(const sf::rectangle &source_roi, const int ctu_size, const int max_width,
                           const int max_height, const std::vector<sf::rectangle> &occupied_ctu_rects) {
        const auto source_ctu = compute_ctu_rectangle(source_roi, ctu_size);

        const auto grid_pos = find_ctu_grid_position(source_ctu.width, source_ctu.height, max_width, max_height,
                                                     ctu_size, occupied_ctu_rects);
        if (!grid_pos) {
            return std::nullopt;
        }

        const sf::rectangle packed_ctu(grid_pos->left, grid_pos->top, source_ctu.width, source_ctu.height);
        const sf::rectangle packed_roi = center_roi_in_ctu_rect(source_roi, packed_ctu);

        return std::pair{packed_roi, packed_ctu};
    }

    /**
     * @brief Place a subsequent-occurrence ROI preserving pixel correspondence.
     *
     * The packed position shifts by the same delta as the source position so that
     * a pixel at packed (x0,y0) in frame N maps to the same source pixel as in frame N-1.
     *
     * @param source_roi Current source-space ROI.
     * @param prev_slot Previous position_slot from memory.
     * @param ctu_size CTU cell size.
     * @param max_width Frame width limit.
     * @param max_height Frame height limit.
     * @param occupied_ctu_rects Currently occupied CTU rectangles (excluding this ROI's previous entry).
     * @return Pair of (packed_roi_position, ctu_rectangle) or nullopt if it doesn't fit.
     */
    [[nodiscard]] std::optional<std::pair<sf::rectangle, sf::rectangle>>
    place_with_correspondence(const sf::rectangle &source_roi, const position_slot &prev_slot, const int ctu_size,
                              const int max_width, const int max_height,
                              const std::vector<sf::rectangle> &occupied_ctu_rects) {
        // Compute source delta
        const int delta_x = source_roi.left - prev_slot.source_roi.left;
        const int delta_y = source_roi.top - prev_slot.source_roi.top;

        // Apply delta to previous packed position
        int new_packed_x = prev_slot.packed_roi.left + delta_x;
        int new_packed_y = prev_slot.packed_roi.top + delta_y;

        // Clamp to non-negative (may slightly break correspondence for one frame)
        new_packed_x = std::max(0, new_packed_x);
        new_packed_y = std::max(0, new_packed_y);

        const sf::rectangle new_packed_roi(new_packed_x, new_packed_y, source_roi.width, source_roi.height);
        const auto new_ctu = compute_ctu_rectangle(new_packed_roi, ctu_size);

        // Check bounds
        if (new_ctu.left + new_ctu.width > max_width || new_ctu.top + new_ctu.height > max_height) {
            return std::nullopt;
        }

        // Check for overlaps with other occupied CTU rectangles
        const bool conflicts = std::ranges::any_of(occupied_ctu_rects, [&new_ctu](const auto &occupied) {
            return ctu_rectangles_overlap(new_ctu, occupied);
        });

        if (conflicts) {
            return std::nullopt;
        }

        return std::pair{new_packed_roi, new_ctu};
    }
} // namespace

std::optional<stable_pack_result> pack_rectangles_with_strategy_indices(
        const std::vector<stable_rectangle> &rectangles, const std::vector<size_t> &strategy_indices,
        const int frame_number, const int max_width, const int max_height, const int ctu_size, int &pack_call_counter,
        std::unordered_map<std::string, position_slot> &position_memory,
        std::unordered_map<std::string, slot_reservation> &slot_reservations, int &compaction_wait_counter,
        size_t &total_stable_count, size_t &total_repacked_count) {
    size_t stable_count = 0;
    size_t repacked_count = 0;

    std::vector<sf::rectangle> packed_rects(rectangles.size());
    std::vector<sf::rectangle> occupied_ctu_rects;
    occupied_ctu_rects.reserve(rectangles.size());

    // Collect IDs of currently present ROIs.
    std::unordered_set<std::string> current_ids;
    current_ids.reserve(rectangles.size());
    for (const auto &rect: rectangles) {
        current_ids.insert(rect.representative_id);
    }

    // Update reservations: decrement timers, add new reservations for recently disappeared ROIs.
    update_slot_reservations(current_ids, position_memory, slot_reservations, pack_call_counter);

    // Reserved CTU rectangles block placement.
    std::vector<sf::rectangle> reserved_ctu_rects;
    reserved_ctu_rects.reserve(slot_reservations.size());
    for (const auto &reservation: slot_reservations | std::views::values) {
        reserved_ctu_rects.push_back(reservation.slot);
    }

    // Process each ROI in priority order.
    for (const size_t strategy_idx: strategy_indices) {
        if (strategy_idx >= rectangles.size()) {
            sf::error(low_delay_packer_debug, "Frame %d: Invalid strategy index %zu (max %zu)", frame_number,
                      strategy_idx, rectangles.size() - 1);
            return std::nullopt;
        }

        const auto &stable_rect = rectangles[strategy_idx];
        const std::string &memory_key = stable_rect.representative_id;

        // Build the full list of obstacles: occupied CTU rects + reserved CTU rects.
        std::vector<sf::rectangle> all_obstacles = occupied_ctu_rects;
        all_obstacles.insert(all_obstacles.end(), reserved_ctu_rects.begin(), reserved_ctu_rects.end());

        bool placed = false;

        // --- Subsequent occurrence: try pixel-correspondence placement ---
        if (const auto memory_it = position_memory.find(memory_key); memory_it != position_memory.end()) {
            const auto &prev_slot = memory_it->second;

            // If the previous slot had valid source_roi data, try correspondence-preserving placement.
            if (prev_slot.source_roi.width > 0 && prev_slot.source_roi.height > 0) {
                if (const auto result = place_with_correspondence(stable_rect.rect, prev_slot, ctu_size, max_width,
                                                                  max_height, all_obstacles)) {
                    const auto &[packed_roi, ctu_rect] = *result;
                    packed_rects[strategy_idx] = packed_roi;
                    occupied_ctu_rects.push_back(ctu_rect);
                    stable_count++;
                    placed = true;

                    memory_it->second.slot_bounds = ctu_rect;
                    memory_it->second.last_roi = stable_rect.rect;
                    memory_it->second.last_pack_call = pack_call_counter;
                    memory_it->second.source_roi = stable_rect.rect;
                    memory_it->second.packed_roi = packed_roi;
                    memory_it->second.is_first_occurrence = false;
                }
            }

            // If correspondence placement failed (e.g., grew and conflicts), relocate this ROI
            // to a free CTU area (do NOT displace other ROIs).
            if (!placed) {
                if (const auto result =
                            place_first_occurrence(stable_rect.rect, ctu_size, max_width, max_height, all_obstacles)) {
                    const auto &[packed_roi, ctu_rect] = *result;
                    packed_rects[strategy_idx] = packed_roi;
                    occupied_ctu_rects.push_back(ctu_rect);
                    repacked_count++;
                    placed = true;

                    memory_it->second.slot_bounds = ctu_rect;
                    memory_it->second.last_roi = stable_rect.rect;
                    memory_it->second.last_pack_call = pack_call_counter;
                    memory_it->second.source_roi = stable_rect.rect;
                    memory_it->second.packed_roi = packed_roi;
                    memory_it->second.is_first_occurrence = true;
                }
            }
        }

        // --- First occurrence: center ROI in CTU rectangle ---
        if (!placed) {
            if (const auto result =
                        place_first_occurrence(stable_rect.rect, ctu_size, max_width, max_height, all_obstacles)) {
                const auto &[packed_roi, ctu_rect] = *result;

                if (!validate_packed_rectangle(packed_roi, stable_rect.rect.width, stable_rect.rect.height)) {
                    return std::nullopt;
                }

                packed_rects[strategy_idx] = packed_roi;
                occupied_ctu_rects.push_back(ctu_rect);
                repacked_count++;

                position_memory[memory_key] = position_slot(ctu_rect, stable_rect.rect, pack_call_counter, memory_key,
                                                            stable_rect.rect, packed_roi, true);
            } else {
                // Could not place this ROI anywhere.
                return std::nullopt;
            }
        }
    }

    if (!validate_all_packed_rectangles(packed_rects)) {
        return std::nullopt;
    }

    const sf::rectangle result_size = calculate_ctu_result_size(occupied_ctu_rects);

    // Compaction: if density is low and there have been repacks, trigger a full CTU-grid compact
    // after a delay.
    if (needs_compaction(packed_rects, result_size, 0.75) && repacked_count > 0) {
        compaction_wait_counter++;
        if (compaction_wait_counter >= 10) {
            compaction_wait_counter = 0;
            if (const auto compact_result =
                        pack_rectangles_compact(rectangles, frame_number, max_width, max_height, ctu_size,
                                                pack_call_counter, position_memory, total_repacked_count)) {
                return compact_result;
            }
        }
    } else {
        compaction_wait_counter = 0;
    }

    total_stable_count += stable_count;
    total_repacked_count += repacked_count;

    return stable_pack_result{packed_rects, result_size, stable_count, repacked_count};
}

std::optional<stable_pack_result>
pack_rectangles_shift_to_origin(const std::vector<stable_rectangle> &rectangles, const int frame_number,
                                const int max_width, const int max_height, const int ctu_size, int &pack_call_counter,
                                std::unordered_map<std::string, position_slot> &position_memory,
                                size_t &total_stable_count, size_t &total_repacked_count) {
    size_t stable_count = 0;
    size_t repacked_count = 0;

    std::vector<sf::rectangle> packed_rects(rectangles.size());
    std::vector<sf::rectangle> occupied_ctu_rects;
    occupied_ctu_rects.reserve(rectangles.size());

    for (size_t i = 0; i < rectangles.size(); ++i) {
        const auto &stable_rect = rectangles[i];
        const std::string &memory_key = stable_rect.representative_id;
        bool placed_at_previous_position = false;

        // Try to reuse previous CTU rectangle.
        if (auto memory_it = position_memory.find(memory_key); memory_it != position_memory.end()) {
            const auto &prev_slot = memory_it->second;
            const sf::rectangle desired_ctu = prev_slot.slot_bounds;

            if (can_fit_at_position(desired_ctu, desired_ctu.left, desired_ctu.top, max_width, max_height,
                                    occupied_ctu_rects)) {
                // Place ROI content at correspondence-preserving position if source data is available,
                // otherwise center it.
                sf::rectangle packed_roi;
                if (prev_slot.source_roi.width > 0 && prev_slot.source_roi.height > 0) {
                    const int delta_x = stable_rect.rect.left - prev_slot.source_roi.left;
                    const int delta_y = stable_rect.rect.top - prev_slot.source_roi.top;
                    packed_roi = sf::rectangle(std::max(0, prev_slot.packed_roi.left + delta_x),
                                               std::max(0, prev_slot.packed_roi.top + delta_y), stable_rect.rect.width,
                                               stable_rect.rect.height);
                } else {
                    packed_roi = center_roi_in_ctu_rect(stable_rect.rect, desired_ctu);
                }
                packed_rects[i] = packed_roi;
                occupied_ctu_rects.push_back(desired_ctu);
                stable_count++;
                placed_at_previous_position = true;

                memory_it->second.last_pack_call = pack_call_counter;
                memory_it->second.last_roi = stable_rect.rect;
                memory_it->second.source_roi = stable_rect.rect;
                memory_it->second.packed_roi = packed_roi;
                memory_it->second.is_first_occurrence = false;
            }
        }

        if (!placed_at_previous_position) {
            // Place as first occurrence using CTU-grid positioning.
            if (const auto result =
                        place_first_occurrence(stable_rect.rect, ctu_size, max_width, max_height, occupied_ctu_rects)) {
                const auto &[packed_roi, ctu_rect] = *result;
                packed_rects[i] = packed_roi;
                occupied_ctu_rects.push_back(ctu_rect);
                repacked_count++;

                position_memory[memory_key] = position_slot(ctu_rect, stable_rect.rect, pack_call_counter, memory_key,
                                                            stable_rect.rect, packed_roi, true);
            } else {
                sf::error(low_delay_packer_debug,
                          "Frame %d: Shift-to-origin failed: could not place ROI %s (%dx%d) on CTU grid", frame_number,
                          stable_rect.id.c_str(), stable_rect.rect.width, stable_rect.rect.height);
                return std::nullopt;
            }
        }
    }

    if (!validate_all_packed_rectangles(packed_rects)) {
        return std::nullopt;
    }

    const sf::rectangle result_size = calculate_ctu_result_size(occupied_ctu_rects);

    total_stable_count += stable_count;
    total_repacked_count += repacked_count;

    return stable_pack_result{packed_rects, result_size, stable_count, repacked_count};
}

std::optional<stable_pack_result>
pack_rectangles_compact(const std::vector<stable_rectangle> &rectangles, const int frame_number, const int max_width,
                        const int max_height, const int ctu_size, int &pack_call_counter,
                        std::unordered_map<std::string, position_slot> &position_memory, size_t &total_repacked_count) {
    const std::vector<size_t> sorted_indices = get_sorted_by_area_desc_indices(rectangles);

    std::vector<sf::rectangle> packed_rects(rectangles.size());
    std::vector<sf::rectangle> occupied_ctu_rects;
    occupied_ctu_rects.reserve(rectangles.size());

    size_t repacked_count = 0;

    for (const size_t idx: sorted_indices) {
        const auto &stable_rect = rectangles[idx];

        // Place as first occurrence on CTU grid (compaction always re-centers).
        if (const auto result =
                    place_first_occurrence(stable_rect.rect, ctu_size, max_width, max_height, occupied_ctu_rects)) {
            const auto &[packed_roi, ctu_rect] = *result;

            if (!validate_packed_rectangle(packed_roi, stable_rect.rect.width, stable_rect.rect.height)) {
                return std::nullopt;
            }

            packed_rects[idx] = packed_roi;
            occupied_ctu_rects.push_back(ctu_rect);
            repacked_count++;

            const std::string &memory_key = stable_rect.representative_id;
            position_memory[memory_key] = position_slot(ctu_rect, stable_rect.rect, pack_call_counter, memory_key,
                                                        stable_rect.rect, packed_roi, true);
        } else {
            sf::error(low_delay_packer_debug, "Frame %d: Failed to find compact CTU position for ROI %s", frame_number,
                      stable_rect.id.c_str());
            return std::nullopt;
        }
    }

    const sf::rectangle result_size = calculate_ctu_result_size(occupied_ctu_rects);

    total_repacked_count += repacked_count;

    return stable_pack_result{packed_rects, result_size, 0, repacked_count};
}
