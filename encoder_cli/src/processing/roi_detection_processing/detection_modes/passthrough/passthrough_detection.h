// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../detection_interface.h"

#include <vector>

/**
 * @file passthrough_detection.height
 * @brief Declares functions for passthrough ROI detection mode (no detection).
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
 * @brief Detects ROIs using passthrough mode (no detection).
 * @param data Global data (unused)
 * @param input_buffer Input buffer (unused)
 * @param props Frame properties (unused)
 * @param frame_number Frame number (unused)
 * @return Pair of (empty input_rectangles, empty split_rectangles)
 */
[[nodiscard]] std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
passthrough_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                        const frame_properties &props, int frame_number);

/**
 * @brief Gets tracked objects for passthrough mode (always empty).
 * @param data Global data (unused)
 * @return Empty vector
 */
[[nodiscard]] std::vector<detected_object> passthrough_get_tracked_objects(global_data &data);
