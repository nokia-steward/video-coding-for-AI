// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <vector>
#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Reconstructs a YUV420 frame by copying packed regions to their target locations.
     * @param packed_map_info Mapped input buffer (packed frame).
     * @param reconstructed_map_info Mapped output buffer (reconstructed frame).
     * @param packed_info Video info for the packed frame.
     * @param reconstructed_info Video info for the reconstructed frame.
     * @param packed_rects Vector of rectangles in the packed frame.
     * @param target_rects Vector of rectangles in the reconstructed (target) frame.
     */
    void reconstruct_yuv420_frame(const GstMapInfo &packed_map_info, const GstMapInfo &reconstructed_map_info,
                                  const GstVideoInfo &packed_info, const GstVideoInfo &reconstructed_info,
                                  const std::vector<rectangle> &packed_rects,
                                  const std::vector<rectangle> &target_rects);
} // namespace sf
