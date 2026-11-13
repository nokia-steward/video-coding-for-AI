// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../processing_interface.h"

/**
 * @file background_blur_processing.height
 * @brief Declares functions for background blur processing mode.
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
 * @brief Processes a frame with ROIs using background blur mode.
 * @param data Global data
 * @param input_buffer Input buffer
 * @param output_buffer Output buffer (may be modified)
 * @param props Frame properties
 * @param input_rectangles Detected rectangles
 * @param split_rectangles Split rectangles
 * @param is_static Whether this is static ROI mode
 * @param frame_number Frame number
 */
void background_blur_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                   sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                   const std::vector<sf::rectangle> &input_rectangles,
                                   const std::vector<split_rectangle> &split_rectangles, bool is_static,
                                   int frame_number);

/**
 * @brief Processes a frame with no ROIs using background blur mode.
 * @param data Global data
 * @param input_buffer Input buffer
 * @param output_buffer Output buffer (may be modified)
 * @param props Frame properties
 */
void background_blur_process_no_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                     sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props);
