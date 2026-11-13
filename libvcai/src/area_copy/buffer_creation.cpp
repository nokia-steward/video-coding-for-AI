// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/area_copy.h"
#include "../../include/logging.h"

#include <cstring>
#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <gst/video/video.h>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Initializes YUV420 buffer with specified background color.
         * @param buffer_out Output buffer.
         * @param map_info Mapped buffer info.
         * @param width Frame width.
         * @param height Frame height.
         * @param background_color Background color enum value.
         */
        void initialize_yuv420_buffer_with_color(unique_gst_buffer_ptr &buffer_out, GstMapInfo &map_info, int width,
                                                 int height, background_color background_color) {
            const size_t luma_size = static_cast<size_t>(width) * static_cast<size_t>(height);
            const size_t uv_size = luma_size / 4; // Each UV plane is 1/4 of Y plane

            uint8_t luma_value = 0x00;
            uint8_t uv_value = 0x80;

            if (background_color == background_color::green) {
                // Background color green: Y=0x00, U=0x00, V=0x00 (green in YUV)
                luma_value = 0x00;
                uv_value = 0x00;
            } else {
                // For other colors, use default values (can be extended later)
                luma_value = 0x00;
                uv_value = 0x80;
            }

            // Initialize Y plane
            std::memset(map_info.data, luma_value, luma_size);

            // Initialize U and V planes
            if (luma_size + uv_size * 2 <= static_cast<size_t>(gst_buffer_get_size(buffer_out.get()))) {
                std::memset(map_info.data + luma_size, uv_value, uv_size * 2);
            }
        }
    } // namespace

    unique_gst_buffer_ptr create_empty_buffer(int width, int height, video_format format,
                                              background_color background_color) {
        if (width <= 0 || height <= 0) {
            error(area_copy, "Invalid dimensions for empty buffer: %dx%d", width, height);
            return nullptr;
        }

        const char *format_str = video_format_to_string(format);

        // Create caps from format and dimensions
        const unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str, "width",
                                                           G_TYPE_INT, width, "height", G_TYPE_INT, height, nullptr));
        if (!caps) {
            error(area_copy, "Failed to create caps for empty buffer");
            return nullptr;
        }

        // Parse video info from caps to get correct buffer size
        GstVideoInfo video_info;
        if (!gst_video_info_from_caps(&video_info, caps.get())) {
            error(area_copy, "Failed to parse video info from caps for format: %s", format_str);
            return nullptr;
        }

        // Allocate buffer with correct size
        auto buffer = unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, video_info.size, nullptr));
        if (!buffer) {
            error(area_copy, "Failed to allocate empty buffer");
            return nullptr;
        }

        // Initialize buffer based on format
        GstMapInfo map_info;
        if (gst_buffer_map(buffer.get(), &map_info, GST_MAP_WRITE)) {
            if (format == video_format::i420 || format == video_format::yuv420) {
                initialize_yuv420_buffer_with_color(buffer, map_info, width, height, background_color);
            } else if (format == video_format::nv12) {
                // For NV12, use the existing initialization logic
                const gsize luma_plane_size =
                        GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0) * GST_VIDEO_INFO_COMP_HEIGHT(&video_info, 0);
                uint8_t luma_value = 0x00;
                uint8_t uv_value = 0x00;

                if (background_color == background_color::green) {
                    luma_value = 0x00;
                    uv_value = 0x00;
                } else {
                    luma_value = 0x00;
                    uv_value = 0x80;
                }

                std::memset(map_info.data + GST_VIDEO_INFO_PLANE_OFFSET(&video_info, 0), luma_value, luma_plane_size);

                const gsize uv_plane_size =
                        GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 1) * GST_VIDEO_INFO_COMP_HEIGHT(&video_info, 1);
                std::memset(map_info.data + GST_VIDEO_INFO_PLANE_OFFSET(&video_info, 1), uv_value, uv_plane_size);
            } else {
                // For other formats, use simple initialization
                if (background_color == background_color::green) {
                    gst_buffer_memset(buffer.get(), 0, 0, video_info.size);
                } else {
                    gst_buffer_memset(buffer.get(), 0, 0, video_info.size);
                }
            }

            gst_buffer_unmap(buffer.get(), &map_info);
        } else {
            // Fallback: just zero the entire buffer
            gst_buffer_memset(buffer.get(), 0, 0, video_info.size);
        }

        return buffer;
    }
} // namespace sf
