// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <vector>
#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Reconstructs an NV12 frame using GstVideoFrame for safe stride/memory handling.
     *
     * Follows the NV12 Video Region Reconstruction protocol:
     * - Uses GstVideoFrame API to handle complex padding and hardware buffers safely
     * - Enforces even-aligned coordinates using bitwise operations
     * - Uses byte-equivalent X coordinates for UV plane (x pixels = x bytes)
     * - Copies w bytes for UV plane (w/2 pixels * 2 bytes = w bytes)
     *
     * @param src_buf Source buffer (packed frame).
     * @param dst_buf Destination buffer (reconstructed frame).
     * @param src_info Video info for the packed frame.
     * @param dst_info Video info for the reconstructed frame.
     * @param packed_rects Vector of rectangles in the packed frame.
     * @param target_rects Vector of rectangles in the reconstructed (target) frame.
     */
    void reconstruct_nv12_frame(const GstBuffer *src_buf, unique_gst_buffer_ptr &dst_buf, GstVideoInfo *src_info,
                                GstVideoInfo *dst_info, const std::vector<rectangle> &packed_rects,
                                const std::vector<rectangle> &target_rects);
} // namespace sf
