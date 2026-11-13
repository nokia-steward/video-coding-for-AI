// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "passthrough_detection.h"
#include "../../../../global_data.h"

#include <utility>
#include <vector>

/**
 * @file passthrough_detection.cpp
 * @brief Implementation of passthrough ROI detection mode (no detection).
 */

/**
 * @brief Detects ROIs using passthrough mode (no detection).
 * @param data Global data (unused)
 * @param input_buffer Input buffer (unused)
 * @param props Frame properties (unused)
 * @param frame_number Frame number (unused)
 * @return Pair of (empty input_rectangles, empty split_rectangles)
 */
std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
passthrough_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                        const frame_properties &props, const int frame_number) {
    (void) data;
    (void) input_buffer;
    (void) props;
    (void) frame_number;

    return std::make_pair(std::vector<sf::rectangle>{}, std::vector<split_rectangle>{});
}

/**
 * @brief Gets tracked objects for passthrough mode (always empty).
 * @param data Global data (unused)
 * @return Empty vector
 */
std::vector<detected_object> passthrough_get_tracked_objects(global_data &data) {
    (void) data;
    return {};
}
