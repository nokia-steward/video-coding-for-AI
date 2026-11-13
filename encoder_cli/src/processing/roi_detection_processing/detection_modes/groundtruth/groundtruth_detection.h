// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../detection_interface.h"

#include <vector>

/**
 * @file groundtruth_detection.height
 * @brief Declares functions for groundtruth ROI detection mode (per-frame COCO annotations).
 *
 * In groundtruth mode, ROIs are looked up from a pre-loaded COCO groundtruth dataset
 * by frame number. This provides "perfect" ROIs for evaluation purposes, decoupling
 * detection quality from the encoder's own inference pipeline.
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
 * @brief Detects ROIs using groundtruth mode (per-frame COCO annotations).
 *
 * Looks up bounding boxes for the given frame_number from the pre-loaded
 * groundtruth_roi_data in global_data. Returns the rectangles directly
 * without any tracking or splitting.
 *
 * @param data Global data (contains loaded_groundtruth_roi_data)
 * @param input_buffer Input buffer (unused for groundtruth mode)
 * @param props Frame properties (unused for groundtruth mode)
 * @param frame_number Current frame number used to look up groundtruth annotations
 * @return Pair of (input_rectangles from groundtruth, empty split_rectangles)
 */
[[nodiscard]] std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
groundtruth_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                        const frame_properties &props, int frame_number);

/**
 * @brief Gets tracked objects for groundtruth mode (always empty).
 *
 * Groundtruth mode does not use tracking, so no tracked objects are reported.
 *
 * @param data Global data (unused)
 * @return Empty vector
 */
[[nodiscard]] std::vector<detected_object> groundtruth_get_tracked_objects(global_data &data);
