// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../detection_interface.h"

#include <vector>

/**
 * @file static_detection.height
 * @brief Declares functions for static ROI detection mode (pre-loaded JSON data).
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
 * @brief Detects ROIs using static mode (pre-loaded JSON data).
 * @param data Global data
 * @param input_buffer Input buffer (unused for static mode)
 * @param props Frame properties (unused for static mode)
 * @param frame_number Frame number (unused for static mode)
 * @return Pair of (input_rectangles, empty split_rectangles)
 */
[[nodiscard]] std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
static_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                   const frame_properties &props, int frame_number);

/**
 * @brief Gets tracked objects for static mode (always empty).
 * @param data Global data (unused)
 * @return Empty vector
 */
[[nodiscard]] std::vector<detected_object> static_get_tracked_objects(global_data &data);
