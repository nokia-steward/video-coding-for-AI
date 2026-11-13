// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include "rectangle.h"

#include "../global_data.h"

/**
 * @file roi_detection_processing.h
 * @brief Declares the main ROI detection frame processing function and related structures.
 */

/**
 * @struct detection_result
 * @brief Represents a detection with confidence and class information.
 */
struct detection_result {
    sf::rectangle bounding_box;
    int class_id;
    float confidence;
    guint64 object_id; // DeepStream's object_id for persistent tracking

    /**
     * @brief Constructs a detection result.
     * @param box Bounding box of the detection.
     * @param cls_id Class ID produced by the detector.
     * @param conf Confidence score for the detection.
     * @param obj_id Optional persistent object ID from DeepStream.
     */
    detection_result(const sf::rectangle &box, const int cls_id, const float conf, const guint64 obj_id = 0) :
        bounding_box(box), class_id(cls_id), confidence(conf), object_id(obj_id) {}
};

/**
 * @brief The main function for the ROI detection frame processing thread.
 *
 * Blocking reference loop for tests and direct use; live path uses GstTask.
 *
 * @param data A reference to the shared global_data struct.
 */
void roi_detection_frame_processing(global_data &data);

/**
 * @brief Starts GstTask-driven ROI detection (one queue pop + process per iteration).
 */
[[nodiscard]] bool start_roi_detection_gst_task(global_data &data, bypass_gst_poll_slot &slot);

/**
 * @brief Stops and joins the ROI detection GstTask.
 */
void join_roi_detection_gst_task(global_data &data, bypass_gst_poll_slot &slot);

/**
 * @brief Initializes the GStreamer debug category for ROI detection processing.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_roi_detection_processing_debug_category();
