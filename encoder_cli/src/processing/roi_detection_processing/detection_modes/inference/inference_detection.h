// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../detection_interface.h"

#include <utility>
#include <vector>

/**
 * @file inference_detection.height
 * @brief Declares functions for inference-based ROI detection mode.
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
 * @brief Detects ROIs using inference mode (DeepStream metadata).
 * @param data Global data
 * @param input_buffer Input buffer
 * @param props Frame properties
 * @param frame_number Frame number
 * @return Pair of (input_rectangles, split_rectangles)
 */
[[nodiscard]] std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
inference_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                      const frame_properties &props, int frame_number);

/**
 * @brief Gets tracked objects for inference mode.
 * @param data Global data
 * @return Vector of tracked objects
 */
[[nodiscard]] std::vector<detected_object> inference_get_tracked_objects(global_data &data);
