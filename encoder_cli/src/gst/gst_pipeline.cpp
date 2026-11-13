// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "gst_pipeline.h"
#include "input_pipeline.h"
#include "output_pipeline.h"
#include "pipeline_utils.h"
#include "rtp_utils.h"

/**
 * @file gst_pipeline.cpp
 * @brief Main GStreamer pipeline creation and management functions.
 */

/// GStreamer debug category for pipeline orchestration.
GST_DEBUG_CATEGORY_STATIC(gst_pipeline);

void init_gst_pipeline_debug_category() {
    GST_DEBUG_CATEGORY_INIT(gst_pipeline, "gst-pipeline", 0, "GStreamer pipeline creation and management operations");

    // Initialize all sub-module debug categories
    init_input_pipeline_debug_category();
    init_output_pipeline_debug_category();
    init_rtp_utils_debug_category();
    init_pipeline_utils_debug_category();
}
