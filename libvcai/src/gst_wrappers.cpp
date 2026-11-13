// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "gst_wrappers.h"
#include <gst/video/video-info.h>
#include <memory>

namespace sf {
    /**
     * @file gst_wrappers.cpp
     * @brief Implementation of clean C++ wrappers around GStreamer C APIs.
     */

    [[nodiscard]] std::unique_ptr<GstMapInfo> map_buffer_safe(GstBuffer *buffer, const GstMapFlags map_flags) {
        if (!buffer) {
            return nullptr;
        }

        auto map_info = std::make_unique<GstMapInfo>();
        if (!gst_buffer_map(buffer, map_info.get(), map_flags)) {
            return nullptr;
        }

        return map_info;
    }

    [[nodiscard]] std::unique_ptr<GstVideoInfo> create_video_info_safe(GstCaps *caps) {
        if (!caps) {
            return nullptr;
        }

        auto video_info = std::make_unique<GstVideoInfo>();
        if (!gst_video_info_from_caps(video_info.get(), caps)) {
            return nullptr;
        }

        return video_info;
    }

    [[nodiscard]] unique_gst_sample_ptr create_sample_safe(GstBuffer *buffer, GstCaps *caps, GstSegment *segment,
                                                           GstStructure *info) {
        if (!buffer || !caps) {
            return nullptr;
        }

        // Create a copy of the info structure if provided
        GstStructure *info_copy = nullptr;
        if (info) {
            info_copy = gst_structure_copy(info);
        }

        auto sample = unique_gst_sample_ptr(gst_sample_new(buffer, caps, segment, info_copy));
        return sample;
    }

    void set_buffer_timing(unique_gst_buffer_ptr &buffer, const buffer_timing &timing) {
        if (!buffer) {
            return;
        }

        GST_BUFFER_PTS(buffer.get()) = timing.pts;
        GST_BUFFER_DTS(buffer.get()) = timing.dts;
        GST_BUFFER_DURATION(buffer.get()) = timing.duration;
    }
} // namespace sf
