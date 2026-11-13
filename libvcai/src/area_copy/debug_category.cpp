// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include "../../include/area_copy.h"

// Debug category for area copy utilities.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

/**
 * @brief Initialize the area copy debug category at load time.
 */
static bool debug_category_initialized = [] {
    GST_DEBUG_CATEGORY_INIT(area_copy, "area-copy", 0, "Area copying utilities");
    return true;
}();

/**
 * @brief Initialize the area_copy debug category.
 * This function can be called explicitly to ensure the category is initialized after gst_init().
 */
void init_area_copy_debug_category() { GST_DEBUG_CATEGORY_INIT(area_copy, "area-copy", 0, "Area copying utilities"); }
