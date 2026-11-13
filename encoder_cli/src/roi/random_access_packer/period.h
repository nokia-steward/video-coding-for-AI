// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include "../low_delay_packer/mapping.h"
#include "../packer_common.h"
#include "history.h"

/**
 * @file period.height
 * @brief Declares intra-period processing functions for random access packer.
 *
 * The period processor accumulates ROIs, merges overlapping ones, and packs them
 * using CTU-aligned two-phase packing with inter-period position memory for
 * pixel correspondence preservation.
 */

/**
 * @brief Forward declaration for the random access packer implementation.
 */
struct random_access_packer;
/**
 * @brief Forward declaration for a complete period of ROIs.
 */
struct complete_period;

/**
 * @brief Process the buffered intra_period: accumulate, merge, and pack ROIs.
 *
 * Uses CTU-aligned two-phase packing:
 * - Phase A: Place recurring ROIs with correspondence-preserving positions.
 * - Phase B: Place new and deferred ROIs as first occurrences.
 *
 * @param object_histories Map of object ROI histories for the current period.
 * @param max_width Maximum width constraint.
 * @param max_height Maximum height constraint.
 * @param ctu_size CTU cell size in pixels.
 * @param pack_call_counter Current pack call counter (will be incremented).
 * @param position_memory Inter-period position memory (read + updated).
 * @return Optional pack result, nullopt if processing fails.
 */
[[nodiscard]] std::optional<pack_result>
process_intra_period(const std::unordered_map<std::string, object_roi_history> &object_histories, int max_width,
                     int max_height, int ctu_size, int &pack_call_counter,
                     std::unordered_map<std::string, position_slot> &position_memory);
