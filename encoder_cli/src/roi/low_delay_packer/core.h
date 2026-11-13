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
 * @file core.height
 * @brief Declares core packing algorithm functions for low delay packer.
 */

/**
 * @brief Forward declaration for rectangle placement metadata.
 */
struct stable_rectangle;
/**
 * @brief Forward declaration for stable packing results.
 */
struct stable_pack_result;

/**
 * @brief Pack rectangles with CTU-aligned position stability (internal method).
 * @param rectangles Vector of rectangles with IDs to pack
 * @param frame_number The frame number for logging purposes
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @param ctu_size CTU cell size in pixels (typically 64)
 * @param pack_call_counter Current pack call counter (will be incremented)
 * @param position_memory Map of remembered positions (will be modified)
 * @param slot_reservations Map of reserved slots for missing ROIs (will be modified)
 * @param compaction_wait_counter Delay counter for compaction (will be modified)
 * @param total_stable_count Running count of stable placements (will be modified)
 * @param total_repacked_count Running count of repacked placements (will be modified)
 * @return Optional pack result, nullopt if packing fails
 */
[[nodiscard]] std::optional<stable_pack_result>
pack_rectangles(const std::vector<stable_rectangle> &rectangles, int frame_number, int max_width, int max_height,
                int ctu_size, int &pack_call_counter, std::unordered_map<std::string, position_slot> &position_memory,
                std::unordered_map<std::string, slot_reservation> &slot_reservations, int &compaction_wait_counter,
                size_t &total_stable_count, size_t &total_repacked_count);
