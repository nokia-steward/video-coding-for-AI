// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "output_pipeline.h"
#include "output_pipeline/output_pipeline_debug.h"

#include <gst/gst.h>

/**
 * @file output_pipeline.cpp
 * @brief Implementation of output pipeline creation and management functions.
 */

GST_DEBUG_CATEGORY(output_pipeline);

void init_output_pipeline_debug_category() {
    GST_DEBUG_CATEGORY_INIT(output_pipeline, "output-pipeline", 0,
                            "Output pipeline creation and management operations");
}
