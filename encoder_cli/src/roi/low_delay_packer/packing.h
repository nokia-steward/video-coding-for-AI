// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../packer_common.h"
#include "mapping.h"
#include "slot_reservation.h"

/**
 * @file packing.height
 * @brief Declares main CTU-aligned packing algorithm functions for low delay packer.
 *
 * The packing algorithm places ROI bounding boxes on a CTU grid in the packed frame:
 * - First occurrence: the ROI is centered within its enclosing CTU rectangle.
 * - Subsequent occurrences: pixel correspondence is preserved — the packed position
 *   shifts by the same delta as the source-space ROI position.
 * - A single CTU cell is occupied by at most one ROI's CTU rectangle.
 * - Shrinking ROIs release CTU rows/columns without moving other ROIs.
 * - Growing ROIs that cannot expand in place are relocated to free CTU cells.
 */

/**
 * @brief Pack rectangles using a specific ordering strategy with CTU-aligned placement.
 * @param rectangles Vector of rectangles with IDs to pack
 * @param strategy_indices Vector of indices defining the processing order
 * @param frame_number The frame number for logging purposes
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param ctu_size CTU cell size in pixels (typically 64)
 * @param pack_call_counter Current pack call counter
 * @param position_memory Map of remembered positions (will be modified)
 * @param slot_reservations Map of reserved slots for missing ROIs (will be modified)
 * @param compaction_wait_counter Delay counter for compaction (will be modified)
 * @param total_stable_count Running count of stable placements (will be modified)
 * @param total_repacked_count Running count of repacked placements (will be modified)
 * @return Optional pack result, nullopt if packing fails
 */
[[nodiscard]] std::optional<stable_pack_result> pack_rectangles_with_strategy_indices(
        const std::vector<stable_rectangle> &rectangles, const std::vector<size_t> &strategy_indices, int frame_number,
        int max_width, int max_height, int ctu_size, int &pack_call_counter,
        std::unordered_map<std::string, position_slot> &position_memory,
        std::unordered_map<std::string, slot_reservation> &slot_reservations, int &compaction_wait_counter,
        size_t &total_stable_count, size_t &total_repacked_count);

/**
 * @brief Pack rectangles using a simple "shift to origin" strategy as final fallback.
 * @param rectangles Vector of rectangles with IDs to pack
 * @param frame_number The frame number for logging purposes
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param ctu_size CTU cell size in pixels
 * @param pack_call_counter Current pack call counter
 * @param position_memory Map of remembered positions (will be modified)
 * @param total_stable_count Running count of stable placements (will be modified)
 * @param total_repacked_count Running count of repacked placements (will be modified)
 * @return Optional pack result, nullopt if packing fails
 */
[[nodiscard]] std::optional<stable_pack_result>
pack_rectangles_shift_to_origin(const std::vector<stable_rectangle> &rectangles, int frame_number, int max_width,
                                int max_height, int ctu_size, int &pack_call_counter,
                                std::unordered_map<std::string, position_slot> &position_memory,
                                size_t &total_stable_count, size_t &total_repacked_count);

/**
 * @brief Perform a full compact repack of all ROIs using CTU-grid positioning.
 * @param rectangles Vector of rectangles with IDs to repack
 * @param frame_number The frame number for logging purposes
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param ctu_size CTU cell size in pixels
 * @param pack_call_counter Current pack call counter
 * @param position_memory Map of remembered positions (will be modified)
 * @param total_repacked_count Running count of repacked placements (will be modified)
 * @return Optional pack result, nullopt if repacking fails
 */
[[nodiscard]] std::optional<stable_pack_result>
pack_rectangles_compact(const std::vector<stable_rectangle> &rectangles, int frame_number, int max_width,
                        int max_height, int ctu_size, int &pack_call_counter,
                        std::unordered_map<std::string, position_slot> &position_memory, size_t &total_repacked_count);
