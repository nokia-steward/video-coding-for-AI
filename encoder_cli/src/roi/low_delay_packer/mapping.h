// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../../../../libvcai/include/rectangle.h"
#include "../packer_common.h"

/**
 * @file mapping.height
 * @brief Declares position memory mapping structures and functions for low delay packer.
 *
 * The position_slot structure tracks the CTU-aligned placement of each ROI across frames.
 * It stores both the CTU rectangle (for occupancy/collision checks) and the source-space
 * ROI coordinates (for computing pixel-correspondence-preserving deltas on subsequent
 * occurrences).
 */

/**
 * @brief Position slot structure for tracking CTU-aligned ROI placements.
 *
 * Fields:
 * - slot_bounds: The CTU-aligned rectangle in the packed frame that this ROI occupies.
 *   Position and size are always multiples of the CTU size.
 * - last_roi: The ROI dimensions (w, h) from the previous frame (used for size change detection).
 * - last_pack_call: The pack_call_counter value when this slot was last used.
 * - representative_id: Stable identifier for the ROI (survives merges).
 * - source_roi: The ROI bounding box in source (original frame) coordinates from the previous
 *   frame. Used to compute the source-space delta for pixel correspondence.
 * - packed_roi: The pixel-level position of the ROI content within the packed frame (may be
 *   offset from slot_bounds origin due to centering).
 * - is_first_occurrence: true when the ROI was centered (first frame), false when
 *   pixel-correspondence tracking is active.
 */
struct position_slot {
    sf::rectangle slot_bounds;
    sf::rectangle last_roi;
    int last_pack_call;
    std::string representative_id;

    /// Source-space ROI bounding box from the previous frame.
    sf::rectangle source_roi;
    /// Pixel-level packed position of the ROI content within the packed frame.
    sf::rectangle packed_roi;
    /// Whether this slot was placed via centering (first occurrence) rather than
    /// pixel-correspondence tracking (subsequent occurrences).
    bool is_first_occurrence;

    position_slot();
    position_slot(const sf::rectangle &slot, const sf::rectangle &roi, int call, std::string representative);
    position_slot(const sf::rectangle &slot, const sf::rectangle &roi, int call, std::string representative,
                  const sf::rectangle &source, const sf::rectangle &packed, bool first_occurrence);
};

/**
 * @brief Check which recurring ROIs can fit at their previous positions without conflicts.
 * @param rectangles Vector of rectangles with IDs to check
 * @param position_memory Map of remembered positions
 * @param max_width Maximum width constraint
 * @param max_height Maximum height constraint
 * @return Pair of vectors: first contains indices of recurring ROIs that can fit,
 *         second contains indices of recurring ROIs that cannot fit
 */
[[nodiscard]] std::pair<std::vector<size_t>, std::vector<size_t>>
get_recurring_rois_fit_status(const std::vector<stable_rectangle> &rectangles,
                              const std::unordered_map<std::string, position_slot> &position_memory, int max_width,
                              int max_height);

/**
 * @brief Get indices sorted with priority: ROIs with previous positions first (sorted by weight),
 *         then ROIs without previous positions (sorted by weight).
 * @param rectangles Vector of rectangles with IDs to sort
 * @param position_memory Map of remembered positions
 * @return Vector of indices in priority order
 */
[[nodiscard]] std::vector<size_t>
get_priority_order_indices(const std::vector<stable_rectangle> &rectangles,
                           const std::unordered_map<std::string, position_slot> &position_memory);
