// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "inference_detection.h"
#include "../../../../../../libvcai/include/logging.h"
#include "../../../../global_data.h"
#include "../../../../gst/input_types/frame_path_meta.h"
#include "../../../../roi/roi_tracker_view.h"
#include "../../../frame_processing_utils.h"
#include "../../../roi_detection_extraction.h"
#include "../../../roi_detection_utils.h"
#include "../../helpers.h"

#include <utility>
#include <vector>

/**
 * @file inference_detection.cpp
 * @brief Implementation of inference-based ROI detection mode.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

/**
 * @brief Gets ROI data for a bypass frame from stored inference data.
 */
static std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
get_roi_data_for_bypass_frame(roi_tracker_view &tracker, global_data &data, const frame_properties &props,
                              const int frame_number) {
    std::vector<split_rectangle> split_rects;
    std::vector<sf::rectangle> rects;

    if (!data.bypass.last_inference_roi.get_for_bypass(split_rects, rects)) {
        // No inference data yet - return empty (first few frames)
        sf::warning(roi_detection_processing, "Bypass frame %d has no inference ROI data available", frame_number);
        return {{}, {}};
    }

    // Update ROI tracker to maintain tracking state even for bypass frames
    // This keeps object IDs consistent across frame types
    tracker.advance_frame_without_detection(frame_number, props.width, props.height);

    return {std::move(rects), std::move(split_rects)};
}

/**
 * @brief Detects ROIs using inference mode (DeepStream metadata).
 * @param data Global data
 * @param input_buffer Input buffer
 * @param props Frame properties
 * @param frame_number Frame number
 * @return Pair of (input_rectangles, split_rectangles)
 */
std::pair<std::vector<sf::rectangle>, std::vector<split_rectangle>>
inference_detect_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                      const frame_properties &props, const int frame_number) {
    roi_tracker_view tracker(data.roi_tracker);

    // Check if this is a bypass frame
    if (is_bypass_frame(input_buffer.get())) {
        // Bypass frame: use stored ROI data from last inference
        return get_roi_data_for_bypass_frame(tracker, data, props, frame_number);
    }

    // Inference frame: extract ROI data from DeepStream metadata
    sf::debug(roi_detection_processing, "Processing frame %d with inference-based ROI detection", frame_number);

    const auto detections = get_detections_from_metadata(input_buffer.get(), data);

    // Process detections through ROI manager
    const auto tracked_objects =
            tracker.process_frame(detections, static_cast<uint32_t>(frame_number), props.width, props.height);

    // Extract split rectangles from tracked objects (handles overlaps)
    const auto split_rectangles = extract_split_rectangles_from_tracked_objects(tracked_objects, props, frame_number);

    // Extract regular rectangles for packing (for compatibility with existing code)
    const auto input_rectangles = extract_rectangles_from_split_rectangles(split_rectangles);

    // Store ROI data for subsequent bypass frames
    const GstClockTime pts = GST_BUFFER_PTS(input_buffer.get());
    data.bypass.last_inference_roi.update(split_rectangles, input_rectangles, pts, static_cast<guint64>(frame_number));

    return std::make_pair(input_rectangles, split_rectangles);
}

/**
 * @brief Gets tracked objects for inference mode.
 * @param data Global data
 * @return Vector of tracked objects
 */
std::vector<detected_object> inference_get_tracked_objects(global_data &data) {
    return roi_tracker_view(data.roi_tracker).get_tracked_objects();
}
