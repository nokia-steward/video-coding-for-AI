// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "core.h"
#include "../../../../libvcai/include/logging.h"
#include "packing.h"

#include <gst/gst.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @file core.cpp
 * @brief Core packing algorithm implementation for low delay packer.
 */

GST_DEBUG_CATEGORY_EXTERN(low_delay_packer_debug);

std::optional<stable_pack_result> pack_rectangles(const std::vector<stable_rectangle> &rectangles,
                                                  const int frame_number, const int max_width, const int max_height,
                                                  const int ctu_size, int &pack_call_counter,
                                                  std::unordered_map<std::string, position_slot> &position_memory,
                                                  std::unordered_map<std::string, slot_reservation> &slot_reservations,
                                                  int &compaction_wait_counter, size_t &total_stable_count,
                                                  size_t &total_repacked_count) {
    if (rectangles.empty()) {
        return stable_pack_result{{}, {0, 0, 0, 0}, 0, 0};
    }

    pack_call_counter++;

    const std::vector<size_t> strategy_indices = get_priority_order_indices(rectangles, position_memory);

    if (const auto result = pack_rectangles_with_strategy_indices(
                rectangles, strategy_indices, frame_number, max_width, max_height, ctu_size, pack_call_counter,
                position_memory, slot_reservations, compaction_wait_counter, total_stable_count,
                total_repacked_count)) {
        return result;
    }

    sf::warning(low_delay_packer_debug, "Frame %d: Primary packing failed, falling back to shift-to-origin",
                frame_number);
    slot_reservations.clear();

    if (const auto shift_result = pack_rectangles_shift_to_origin(rectangles, frame_number, max_width, max_height,
                                                                  ctu_size, pack_call_counter, position_memory,
                                                                  total_stable_count, total_repacked_count)) {
        return shift_result;
    }

    sf::warning(low_delay_packer_debug, "Frame %d: Shift-to-origin fallback failed, attempting compact repack",
                frame_number);
    slot_reservations.clear();
    if (const auto compact_result = pack_rectangles_compact(rectangles, frame_number, max_width, max_height, ctu_size,
                                                            pack_call_counter, position_memory, total_repacked_count)) {
        return compact_result;
    }

    sf::error(low_delay_packer_debug, "Frame %d: Compact repack fallback failed", frame_number);
    return std::nullopt;
}
