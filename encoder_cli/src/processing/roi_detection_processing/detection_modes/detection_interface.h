// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../../libvcai/include/rectangle.h"
#include "../../../global_data.h"
#include "../../../roi/rectangle_splitter.h"
#include "../../../roi/roi_manager.h"
#include "../roi_strategy_snapshot.h"

#include <functional>
#include <utility>
#include <vector>

/**
 * @file detection_interface.height
 * @brief Declares the interface for ROI detection mode implementations.
 */

/**
 * @brief Forward declaration for frame dimension metadata.
 */
struct frame_properties;
/**
 * @brief Forward declaration for shared application state.
 */
struct global_data;

/**
 * @brief Function table for ROI detection modes.
 *
 * All detection modes must implement these functions to provide a consistent interface.
 */
struct detection_mode_functions {
    /**
     * @brief Detects ROIs from input buffer.
     * @param data Global data
     * @param input_buffer Input buffer
     * @param props Frame properties
     * @param frame_number Frame number
     * @return Pair of (input_rectangles, split_rectangles)
     */
    std::function<std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>(
            global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer, const frame_properties &props,
            int frame_number)>
            detect_rois;

    /**
     * @brief Checks if this detection mode is enabled.
     * @param snapshot Availability/config snapshot
     * @return true if enabled, false otherwise
     */
    std::function<bool(const roi_strategy_snapshot &snapshot)> is_enabled;

    /**
     * @brief Gets tracked objects for status reporting (if applicable).
     * @param data Global data
     * @return Vector of tracked objects (empty if not applicable)
     */
    std::function<std::vector<detected_object>(global_data &data)> get_tracked_objects;
};

/**
 * @brief Context for a detection mode.
 */
struct detection_mode_context {
    const char *name;
    detection_mode_functions functions;
    bool is_enabled;
};
