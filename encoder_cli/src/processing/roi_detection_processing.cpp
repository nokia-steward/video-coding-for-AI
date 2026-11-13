// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file roi_detection_processing.cpp
 * @brief Main processing functions for ROI detection pipeline.
 */

#include "roi_detection_processing.h"
#include "../../libvcai/include/logging.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY(roi_detection_processing);

void init_roi_detection_processing_debug_category() {
    GST_DEBUG_CATEGORY_INIT(roi_detection_processing, "roi-detection-processing", 0,
                            "ROI detection processing using DeepStream inference and tracking");
}
