// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <optional>
#include <utility>
#include "../../../include/area_copy.h"

namespace sf {
    /**
     * @brief Creates and validates GstVideoInfo structures for NV12 processing.
     * @param input_width Input frame width.
     * @param input_height Input frame height.
     * @param input_buffer Input buffer.
     * @param output_buffer Output buffer.
     * @return Optional tuple of (input_info, output_info) if successful.
     */
    [[nodiscard]] std::optional<std::pair<GstVideoInfo, GstVideoInfo>>
    setup_nv12_video_info(const int input_width, const int input_height, const GstBuffer *input_buffer,
                          GstBuffer *output_buffer);

    /**
     * @brief Maps NV12 frames using GstVideoFrame API.
     * @param input_info Input video info.
     * @param output_info Output video info.
     * @param input_buffer Input buffer.
     * @param output_buffer Output buffer.
     * @return Optional tuple of (input_frame, output_frame) if successful.
     */
    [[nodiscard]] std::optional<std::pair<GstVideoFrame, GstVideoFrame>> map_nv12_frames(GstVideoInfo *input_info,
                                                                                         GstVideoInfo *output_info,
                                                                                         const GstBuffer *input_buffer,
                                                                                         GstBuffer *output_buffer);
} // namespace sf
