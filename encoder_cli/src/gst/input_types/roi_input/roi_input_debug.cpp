// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>

#include "../roi_input.h"

GST_DEBUG_CATEGORY(roi_input);

void init_roi_input_debug_category() {
    GST_DEBUG_CATEGORY_INIT(roi_input, "roi-input", 0, "ROI input pipeline operations");
}
