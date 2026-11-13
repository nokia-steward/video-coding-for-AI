// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/area_copy.h"

#include <cstring>

namespace sf {
    const char *video_format_to_string(video_format format) {
        switch (format) {
            case video_format::i420:
                return "I420";
            case video_format::nv12:
                return "NV12";
            case video_format::yuv420:
                return "YUV420";
            default:
                return "I420";
        }
    }

    video_format string_to_video_format(const char *format_str) {
        if (!format_str) {
            return video_format::i420;
        }

        if (std::strcmp(format_str, "I420") == 0) {
            return video_format::i420;
        }
        if (std::strcmp(format_str, "NV12") == 0) {
            return video_format::nv12;
        }
        if (std::strcmp(format_str, "YUV420") == 0) {
            return video_format::yuv420;
        }

        // Default to i420 if not recognized
        return video_format::i420;
    }
} // namespace sf
