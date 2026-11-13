// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include "../../../include/area_copy.h"

namespace sf {
    /**
     * @brief Initializes output buffer based on video format.
     * @param buffer_out Output buffer (non-owning reference).
     * @param video_info_out Output video info.
     * @param format Video format.
     * @param background_color Background color enum value (green for YUV formats).
     */
    void initialize_reconstruction_buffer(unique_gst_buffer_ptr &buffer_out, const GstVideoInfo &video_info_out,
                                          const GstVideoFormat format,
                                          background_color background_color = background_color::green);
} // namespace sf
