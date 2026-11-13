// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "pipeline.h"

#include <gst/gst.h>

// Debug category for pipeline lifecycle and data flow.
GST_DEBUG_CATEGORY(pipeline_category);
#define GST_CAT_DEFAULT pipeline_category

/**
 * @brief Initialize the decoded frame processing debug category.
 */
void init_pipeline_debug_category() { GST_DEBUG_CATEGORY_INIT(pipeline_category, "pipeline", 0, "Gstreamer pipeline"); }
