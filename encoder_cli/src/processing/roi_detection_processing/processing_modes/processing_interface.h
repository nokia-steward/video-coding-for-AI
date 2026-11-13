// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../../libvcai/include/rectangle.h"
#include "../../../global_data.h"
#include "../../../roi/rectangle_splitter.h"
#include "../roi_strategy_snapshot.h"

#include <functional>
#include <vector>

/**
 * @file processing_interface.height
 * @brief Declares the interface for ROI processing mode implementations.
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
 * @brief Function table for ROI processing modes.
 */
struct processing_mode_functions {
    /**
     * @brief Processes a frame with detected ROIs.
     * @param data Global data
     * @param input_buffer Input buffer
     * @param output_buffer Output buffer (may be modified)
     * @param props Frame properties
     * @param input_rectangles Detected rectangles
     * @param split_rectangles Split rectangles
     * @param is_static Whether this is static ROI mode
     * @param frame_number Frame number
     */
    std::function<void(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                       sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                       const std::vector<sf::rectangle> &input_rectangles,
                       const std::vector<split_rectangle> &split_rectangles, bool is_static, int frame_number)>
            process_frame;

    /**
     * @brief Processes a frame with no ROIs detected.
     * @param data Global data
     * @param input_buffer Input buffer
     * @param output_buffer Output buffer (may be modified)
     * @param props Frame properties
     */
    std::function<void(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                       sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props)>
            process_no_rois;

    /**
     * @brief Checks if this processing mode is enabled.
     * @param snapshot Availability/config snapshot
     * @return true if enabled, false otherwise
     */
    std::function<bool(const roi_strategy_snapshot &snapshot)> is_enabled;
};

/**
 * @brief Context for a processing mode.
 */
struct processing_mode_context {
    const char *name;
    processing_mode_functions functions;
    bool is_enabled;
};
