// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "static_detection.h"
#include "../../../../global_data.h"

#include <utility>
#include <vector>

/**
 * @file static_detection.cpp
 * @brief Implementation of static ROI detection mode (pre-loaded JSON data).
 */

/**
 * @brief Detects ROIs using static mode (pre-loaded JSON data).
 * @param data Global data
 * @param input_buffer Input buffer (unused for static mode)
 * @param props Frame properties (unused for static mode)
 * @param frame_number Frame number (unused for static mode)
 * @return Pair of (input_rectangles, empty split_rectangles)
 */
std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
static_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                   const frame_properties &props, const int frame_number) {
    (void) input_buffer;
    (void) props;
    (void) frame_number;

    // Convert static ROI data to rectangles
    const auto input_rectangles = data.loaded_static_roi_data->to_rectangles();
    return std::make_pair(input_rectangles, std::vector<split_rectangle>{});
}

/**
 * @brief Gets tracked objects for static mode (always empty).
 * @param data Global data (unused)
 * @return Empty vector
 */
std::vector<detected_object> static_get_tracked_objects(global_data &data) {
    (void) data;
    return {};
}
