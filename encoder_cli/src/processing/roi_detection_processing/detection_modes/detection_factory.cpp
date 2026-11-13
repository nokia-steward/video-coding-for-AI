// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "detection_factory.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../global_data.h"
#include "groundtruth/groundtruth_detection.h"
#include "inference/inference_detection.h"
#include "passthrough/passthrough_detection.h"
#include "static/static_detection.h"

#include <string>
#include <vector>

/**
 * @file detection_factory.cpp
 * @brief Implementation of detection mode factory functions.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

/**
 * @brief Creates all detection mode contexts.
 * @return Vector of all detection mode contexts
 */
std::vector<detection_mode_context> create_detection_mode_contexts() {
    std::vector<detection_mode_context> contexts;

    // Inference mode
    contexts.push_back(
            {.name = "inference",
             .functions = {.detect_rois = inference_detect_rois,
                           .is_enabled =
                                   [](const roi_strategy_snapshot &snapshot) { return snapshot.inference_engine; },
                           .get_tracked_objects = inference_get_tracked_objects},
             .is_enabled = false});

    // Groundtruth mode (COCO per-frame annotations)
    contexts.push_back({.name = "groundtruth",
                        .functions = {.detect_rois = groundtruth_detect_rois,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return snapshot.groundtruth_roi_available;
                                              },
                                      .get_tracked_objects = groundtruth_get_tracked_objects},
                        .is_enabled = false});

    // Static mode
    contexts.push_back(
            {.name = "static",
             .functions = {.detect_rois = static_detect_rois,
                           .is_enabled =
                                   [](const roi_strategy_snapshot &snapshot) { return snapshot.static_roi_available; },
                           .get_tracked_objects = static_get_tracked_objects},
             .is_enabled = false});

    // Passthrough mode
    contexts.push_back({.name = "passthrough",
                        .functions = {.detect_rois = passthrough_detect_rois,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return !snapshot.inference_engine &&
                                                         !snapshot.groundtruth_roi_available &&
                                                         !snapshot.static_roi_available;
                                              },
                                      .get_tracked_objects = passthrough_get_tracked_objects},
                        .is_enabled = false});

    return contexts;
}

/**
 * @brief Gets the active detection mode based on configuration.
 * @param snapshot Availability/config snapshot
 * @return Pointer to active detection mode context, or nullptr if none enabled
 */
const detection_mode_context *get_active_detection_mode(const roi_strategy_snapshot &snapshot) {
    static const auto contexts = create_detection_mode_contexts();

    for (const auto &context: contexts) {
        if (context.functions.is_enabled(snapshot)) {
            sf::info(roi_detection_processing, "Active detection mode: %s", context.name);
            return &context;
        }
    }

    sf::warning(roi_detection_processing, "No detection mode enabled, defaulting to passthrough");
    // Return passthrough as default
    for (const auto &context: contexts) {
        if (std::string(context.name) == "passthrough") {
            return &context;
        }
    }

    return nullptr;
}
