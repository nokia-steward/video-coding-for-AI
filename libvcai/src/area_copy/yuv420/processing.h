// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <vector>
#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Processes a YUV420 frame by copying specified regions.
     * @param in_map_info Mapped input buffer.
     * @param out_map_info Mapped output buffer.
     * @param input_width Input frame width.
     * @param input_height Input frame height.
     * @param input_rectangles Input rectangles.
     * @param packed_rectangles Packed rectangles.
     */
    void process_yuv420_frame(const GstMapInfo &in_map_info, const GstMapInfo &out_map_info, const int input_width,
                              const int input_height, const std::vector<rectangle> &input_rectangles,
                              const std::vector<rectangle> &packed_rectangles);
} // namespace sf
