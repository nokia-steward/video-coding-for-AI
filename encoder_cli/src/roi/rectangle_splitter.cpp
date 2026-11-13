// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rectangle_splitter.h"

#include <gst/gst.h>

/**
 * @file rectangle_splitter.cpp
 * @brief Implementation of rectangle splitter debug category initialization.
 */

GST_DEBUG_CATEGORY(rectangle_splitter);

void init_rectangle_splitter_debug_category() {
    GST_DEBUG_CATEGORY_INIT(rectangle_splitter, "rectangle-splitter", 0, "Rectangle splitting for overlapping objects");
}
