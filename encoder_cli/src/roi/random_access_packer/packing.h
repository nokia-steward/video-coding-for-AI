// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../low_delay_packer/mapping.h"
#include "../packer_common.h"

/**
 * @file packing.height
 * @brief Declares CTU-aligned packing algorithm functions for random access packer.
 *
 * The packing algorithm places ROI bounding boxes on a CTU grid (64×64 by default).
 * It supports two-phase packing:
 * - Phase A: Place recurring ROIs at correspondence-preserving positions (constraints 3, 4, 5).
 * - Phase B: Place new and deferred ROIs at first-occurrence positions (constraint 2).
 */

/**
 * @brief Pack rectangles using CTU-aligned two-phase packing.
 *
 * Phase A places recurring ROIs (those with entries in position_memory) at
 * correspondence-preserving positions, handling shrink/grow. Phase B places
 * new ROIs and any recurring ROIs that couldn't be placed in Phase A.
 *
 * @param rectangles Vector of stable_rectangles to pack (merged ROIs with composite IDs).
 * @param max_width Maximum frame width.
 * @param max_height Maximum frame height.
 * @param ctu_size CTU cell size in pixels.
 * @param pack_call_counter Current pack call counter.
 * @param position_memory Inter-period position memory (read + updated).
 * @return Optional stable_pack_result, nullopt if packing fails.
 */
[[nodiscard]] std::optional<stable_pack_result>
pack_rectangles_ctu_aligned(const std::vector<stable_rectangle> &rectangles, int max_width, int max_height,
                            int ctu_size, int pack_call_counter,
                            std::unordered_map<std::string, position_slot> &position_memory);
