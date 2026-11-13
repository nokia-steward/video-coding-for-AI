// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "low_delay_packer.h"
#include "../../../libvcai/include/logging.h"

#include <gst/gst.h>

/**
 * @file low_delay_packer.cpp
 * @brief Implementation of low delay packer API functions.
 */

GST_DEBUG_CATEGORY(low_delay_packer_debug);

void init_low_delay_packer_debug_category() {
    GST_DEBUG_CATEGORY_INIT(low_delay_packer_debug, "low-delay-packer", 0,
                            "Low delay rectangle packing algorithm with ROI accumulation");
}
