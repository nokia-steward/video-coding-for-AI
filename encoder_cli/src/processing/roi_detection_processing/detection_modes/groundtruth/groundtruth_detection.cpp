// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "groundtruth_detection.h"
#include "../../../../global_data.h"
#include "../../../../roi/groundtruth_roi_data.h"

#include "../../../../../../libvcai/include/logging.h"

#include <utility>
#include <vector>

/**
 * @file groundtruth_detection.cpp
 * @brief Implementation of groundtruth ROI detection mode (per-frame COCO annotations).
 *
 * In this mode, ROIs come directly from pre-loaded COCO groundtruth data
 * rather than from real-time inference. This enables evaluation of the
 * encoder's ROI processing pipeline (blur, packing) independently of
 * detection accuracy.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

/**
 * @brief Detects ROIs using groundtruth mode (per-frame COCO annotations).
 * @param data Global data (contains loaded_groundtruth_roi_data)
 * @param input_buffer Input buffer (unused for groundtruth mode)
 * @param props Frame properties (unused for groundtruth mode)
 * @param frame_number Current frame number used to look up groundtruth annotations
 * @return Pair of (input_rectangles from groundtruth, empty split_rectangles)
 */
/// @brief Tracks whether the first-frame info log has been emitted.
static bool first_frame_logged = false;

std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
groundtruth_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                        const frame_properties &props, const int frame_number) {
    (void) input_buffer;
    (void) props;

    if (!data.loaded_groundtruth_roi_data.has_value()) {
        sf::warning(roi_detection_processing,
                    "Groundtruth detection mode active but no groundtruth data loaded (frame %d)", frame_number);
        return {{}, {}};
    }

    const auto input_rectangles = data.loaded_groundtruth_roi_data->get_rectangles(frame_number);

    // Log the first frame at INFO level so the log confirms groundtruth ROIs
    // are actually being served. Subsequent frames use DEBUG to avoid noise.
    if (!first_frame_logged) {
        sf::info(roi_detection_processing,
                 "Groundtruth detection mode active — frame %d: %zu ROIs (from %zu total frames in groundtruth)",
                 frame_number, input_rectangles.size(), data.loaded_groundtruth_roi_data->size());
        first_frame_logged = true;
    } else {
        sf::debug(roi_detection_processing, "Groundtruth frame %d: %zu ROIs", frame_number, input_rectangles.size());
    }

    return std::make_pair(input_rectangles, std::vector<split_rectangle>{});
}

/**
 * @brief Gets tracked objects for groundtruth mode (always empty).
 * @param data Global data (unused)
 * @return Empty vector
 */
std::vector<detected_object> groundtruth_get_tracked_objects(global_data &data) {
    (void) data;
    return {};
}
