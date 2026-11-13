// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <vector>
#include "../roi_manager.h"

/**
 * @file tracking.height
 * @brief Declares object tracking algorithms for ROI manager.
 */

/**
 * @brief Forward declaration for detection results produced by ROI extraction.
 */
struct detection_result;

/**
 * @brief Updates existing objects with new detections.
 * @param tracked_objects Vector of tracked objects (will be modified)
 * @param detections Vector of new detections
 * @param frame_number Current frame number
 * @param min_confidence Minimum confidence threshold
 * @param accumulation_mode ROI accumulation mode (object_id or overlap)
 * @param overlap_threshold Overlap threshold for IOU-based matching
 */
void update_existing_objects(std::vector<detected_object> &tracked_objects,
                             const std::vector<detection_result> &detections, uint32_t frame_number,
                             float min_confidence, roi_accumulation_mode accumulation_mode, float overlap_threshold,
                             const stabilization_config &stabilization);

/**
 * @brief Creates new objects for detections that don't match existing objects.
 * @param tracked_objects Vector of tracked objects (will be modified)
 * @param detections Vector of new detections
 * @param frame_number Current frame number
 * @param min_confidence Minimum confidence threshold
 * @param accumulation_mode ROI accumulation mode (object_id or overlap)
 * @param overlap_threshold Overlap threshold for IOU-based matching
 */
void create_new_objects(std::vector<detected_object> &tracked_objects, const std::vector<detection_result> &detections,
                        uint32_t frame_number, float min_confidence, roi_accumulation_mode accumulation_mode,
                        float overlap_threshold);

/**
 * @brief Handles objects that were not updated in this frame.
 * @param tracked_objects Vector of tracked objects (will be modified)
 * @param frame_number Current frame number
 * @param frame_width Width of the frame for boundary checking
 * @param frame_height Height of the frame for boundary checking
 * @param max_missing_frames Maximum frames to track missing objects
 * @param enable_prediction Whether to enable ROI prediction when tracker stops providing data
 */
void handle_missing_objects(std::vector<detected_object> &tracked_objects, uint32_t frame_number, int frame_width,
                            int frame_height, uint32_t max_missing_frames, bool enable_prediction);

/**
 * @brief Finds an existing object by ID.
 * @param tracked_objects Vector of tracked objects
 * @param object_id The ID to search for
 * @return Iterator to the object, or end() if not found
 */
[[nodiscard]] std::vector<detected_object>::iterator find_object_by_id(std::vector<detected_object> &tracked_objects,
                                                                       uint32_t object_id);
