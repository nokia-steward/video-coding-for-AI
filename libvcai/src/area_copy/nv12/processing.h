// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/video/video.h>
#include <vector>
#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Processes an NV12 frame by copying specified regions using GstVideoFrame API.
     * NV12 format has Y plane and interleaved UV plane.
     * Uses GstVideoFrame API for safe stride/memory handling with hardware buffers.
     * @param input_buffer Input buffer.
     * @param output_buffer Output buffer.
     * @param input_width Input frame width.
     * @param input_height Input frame height.
     * @param input_rectangles Input rectangles.
     * @param packed_rectangles Packed rectangles.
     */
    void process_nv12_frame(const GstBuffer *input_buffer, GstBuffer *output_buffer, const int input_width,
                            const int input_height, const std::vector<rectangle> &input_rectangles,
                            const std::vector<rectangle> &packed_rectangles);
} // namespace sf
