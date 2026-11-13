// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "packing_factory.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../encoder_enums.h"
#include "../../../global_data.h"
#include "low_delay/low_delay_packing.h"
#include "random_access/random_access_packing.h"

#include <string>
#include <vector>

/**
 * @file packing_factory.cpp
 * @brief Implementation of packing mode factory functions.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

/**
 * @brief Creates all packing mode contexts.
 * @return Vector of all packing mode contexts
 */
std::vector<packing_mode_context> create_packing_mode_contexts() {
    std::vector<packing_mode_context> contexts;

    // Low delay mode
    contexts.push_back({.name = "low_delay",
                        .functions = {.process_frame = low_delay_process_frame,
                                      .get_packed_rectangles = low_delay_get_packed_rectangles,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return snapshot.pack_rois &&
                                                         snapshot.packer_type_value == packer_type::low_delay;
                                              }},
                        .is_enabled = false});

    // Random access mode
    contexts.push_back({.name = "random_access",
                        .functions = {.process_frame = random_access_process_frame,
                                      .get_packed_rectangles = random_access_get_packed_rectangles,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return snapshot.pack_rois &&
                                                         snapshot.packer_type_value == packer_type::random_access;
                                              }},
                        .is_enabled = false});

    return contexts;
}

/**
 * @brief Gets the active packing mode based on configuration.
 * @param snapshot Availability/config snapshot
 * @return Pointer to active packing mode context, or nullptr if packing disabled
 */
const packing_mode_context *get_active_packing_mode(const roi_strategy_snapshot &snapshot) {
    if (!snapshot.pack_rois) {
        return nullptr;
    }

    static const auto contexts = create_packing_mode_contexts();

    for (const auto &context: contexts) {
        if (context.functions.is_enabled(snapshot)) {
            sf::debug(roi_detection_processing, "Active packing mode: %s", context.name);
            return &context;
        }
    }

    return nullptr;
}
