// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "processing_factory.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../global_data.h"
#include "background_blur/background_blur_processing.h"
#include "packing/packing_processing.h"
#include "qp_map_delta/qp_map_delta_processing.h"

#include <string>
#include <vector>

/**
 * @file processing_factory.cpp
 * @brief Implementation of processing mode factory functions.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    /**
     * @brief Passthrough processing when ROI packing is disabled.
     *
     * With packing, background, and QP-map delta all off, each frame is copied
     * through unchanged by delegating to the existing no-ROI packing behavior —
     * there is nothing to pack, so any frame (with or without detected ROIs)
     * is emitted as-is on an explicit rather than implicit path.
     */
    void passthrough_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                   sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                   const std::vector<sf::rectangle> &input_rectangles,
                                   const std::vector<split_rectangle> &split_rectangles, bool is_static,
                                   int frame_number) {
        (void) input_rectangles;
        (void) split_rectangles;
        (void) is_static;
        (void) frame_number;
        packing_process_no_rois(data, input_buffer, output_buffer, props);
    }

    void passthrough_process_no_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                     sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props) {
        packing_process_no_rois(data, input_buffer, output_buffer, props);
    }
} // namespace

/**
 * @brief Creates all processing mode contexts.
 * @return Vector of all processing mode contexts
 */
std::vector<processing_mode_context> create_processing_mode_contexts() {
    std::vector<processing_mode_context> contexts;

    // Packing mode
    contexts.push_back({.name = "packing",
                        .functions = {.process_frame = packing_process_frame,
                                      .process_no_rois = packing_process_no_rois,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return snapshot.pack_rois &&
                                                         !snapshot.is_background_processing_enabled();
                                              }},
                        .is_enabled = false});

    // Background blur mode
    contexts.push_back({.name = "background_blur",
                        .functions = {.process_frame = background_blur_process_frame,
                                      .process_no_rois = background_blur_process_no_rois,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return snapshot.is_background_blur_effective();
                                              }},
                        .is_enabled = false});

    // QP-map delta mode
    contexts.push_back({.name = "qp_map_delta",
                        .functions = {.process_frame = qp_map_delta_process_frame,
                                      .process_no_rois = qp_map_delta_process_no_rois,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return snapshot.is_qp_map_delta_effective();
                                              }},
                        .is_enabled = false});

    // Passthrough mode: enabled when ROI packing is disabled and no background/QP-map processing is active.
    contexts.push_back({.name = "passthrough",
                        .functions = {.process_frame = passthrough_process_frame,
                                      .process_no_rois = passthrough_process_no_rois,
                                      .is_enabled =
                                              [](const roi_strategy_snapshot &snapshot) {
                                                  return !snapshot.pack_rois &&
                                                         !snapshot.is_background_processing_enabled();
                                              }},
                        .is_enabled = false});

    return contexts;
}

/**
 * @brief Gets the active processing mode based on configuration.
 * @param snapshot Availability/config snapshot
 * @return Pointer to active processing mode context
 */
const processing_mode_context *get_active_processing_mode(const roi_strategy_snapshot &snapshot) {
    static const auto contexts = create_processing_mode_contexts();

    for (const auto &context: contexts) {
        if (context.functions.is_enabled(snapshot)) {
            sf::debug(roi_detection_processing, "Active processing mode: %s", context.name);
            return &context;
        }
    }

    return nullptr;
}
