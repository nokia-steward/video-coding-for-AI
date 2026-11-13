// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "random_access_packer.h"
#include "../../libvcai/include/logging.h"

#include <gst/gst.h>

/**
 * @file random_access_packer.cpp
 * @brief Implementation of random access packer debug category initialization.
 */

/// GStreamer debug category for random access packing.
GST_DEBUG_CATEGORY_STATIC(random_access_packer_debug);

void init_random_access_packer_debug_category() {
    GST_DEBUG_CATEGORY_INIT(random_access_packer_debug, "random-access-packer", 0,
                            "Random access rectangle packing algorithm with intra-period buffering");
}
