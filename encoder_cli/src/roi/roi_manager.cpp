// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "roi_manager.h"

#include <gst/gst.h>

/**
 * @file roi_manager.cpp
 * @brief Implementation of ROI manager debug category initialization.
 */

/// GStreamer debug category for ROI manager operations.
GST_DEBUG_CATEGORY_STATIC(roi_manager);

void init_roi_manager_debug_category() {
    GST_DEBUG_CATEGORY_INIT(roi_manager, "roi-manager", 0, "ROI management and object tracking operations");
}
