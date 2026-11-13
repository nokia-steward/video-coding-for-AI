// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../../libvcai/include/logging.h"
#include "../../processing/roi_detection_processing.h"
#include "../roi_manager.h"
#include "tracking.h"

#include <cstdint>
#include <gst/gst.h>
#include <vector>

/**
 * @file api.cpp
 * @brief Implementation of ROI manager API functions.
 */

/// External GStreamer debug category for ROI manager logging.
GST_DEBUG_CATEGORY_EXTERN(roi_manager);

roi_manager::roi_manager(const float min_confidence_param, const uint32_t max_missing_frames_param,
                         const bool enable_prediction_param, const roi_accumulation_mode accumulation_mode_param,
                         const float overlap_threshold_param, const stabilization_config stabilization_config_param) :
    min_confidence(min_confidence_param),
    max_missing_frames(max_missing_frames_param), enable_prediction(enable_prediction_param),
    accumulation_mode(accumulation_mode_param), overlap_threshold(overlap_threshold_param),
    stabilization(stabilization_config_param), tracked_objects() {}

std::vector<detected_object> roi_manager::process_frame(const std::vector<detection_result> &detections,
                                                        const uint32_t frame_number, const int frame_width,
                                                        const int frame_height) {
    // Update tracked objects that match incoming detections.
    update_existing_objects(tracked_objects, detections, frame_number, min_confidence, accumulation_mode,
                            overlap_threshold, stabilization);

    // Create new tracked objects for unmatched detections.
    create_new_objects(tracked_objects, detections, frame_number, min_confidence, accumulation_mode, overlap_threshold);

    // Advance or predict objects that did not receive detections.
    handle_missing_objects(tracked_objects, frame_number, frame_width, frame_height, max_missing_frames,
                           enable_prediction);

    // Drop expired objects so they no longer contribute to output.
    remove_expired_objects();

    return tracked_objects;
}

void roi_manager::remove_expired_objects() {
    std::erase_if(tracked_objects, [](const detected_object &obj) { return obj.state == object_state::expired; });
}

/**
 * @brief Finds a tracked object by ID (mutable).
 * @param object_id Object identifier.
 * @return Iterator to the object, or end().
 */
std::vector<detected_object>::iterator roi_manager::find_object_by_id(const uint32_t object_id) {
    return ::find_object_by_id(tracked_objects, object_id);
}

/**
 * @brief Finds a tracked object by ID (const).
 * @param object_id Object identifier.
 * @return Const iterator to the object, or end().
 */
std::vector<detected_object>::const_iterator roi_manager::find_object_by_id(const uint32_t object_id) const {
    // Use const_cast to call the free function that takes non-const reference
    // This is safe because find_object_by_id only searches, it doesn't modify
    return ::find_object_by_id(const_cast<std::vector<detected_object> &>(tracked_objects), object_id);
}

void roi_manager::advance_frame_without_detection(const int frame_number, const int width, const int height) {
    // For bypass frames, we don't have new detections from DeepStream.
    // We need to:
    // 1. Increment missing_frames_count for all tracked objects
    // 2. Generate predictions if enabled
    // 3. Expire objects that have been missing too long
    //
    // Pass empty detection list to process_frame - this triggers
    // the "missing object" handling for all currently tracked objects
    const std::vector<detection_result> empty_detections;
    (void) process_frame(empty_detections, static_cast<uint32_t>(frame_number), width, height);
}
