// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"

#include <cstring>
#include <gst/video/video-info.h>
#include <gst/video/video.h>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Initializes NV12 buffer with specified background color.
         * @param buffer_out Output buffer.
         * @param video_info_out Output video info.
         * @param background_color Background color enum value (green: Y=0x00, U=0x00, V=0x00).
         */
        void initialize_nv12_buffer_with_color(unique_gst_buffer_ptr &buffer_out, const GstVideoInfo &video_info_out,
                                               background_color background_color) {
            GstMapInfo init_map_info;
            if (gst_buffer_map(buffer_out.get(), &init_map_info, GST_MAP_WRITE)) {
                const gsize luma_plane_size = GST_VIDEO_INFO_PLANE_STRIDE(&video_info_out, 0) *
                                              GST_VIDEO_INFO_COMP_HEIGHT(&video_info_out, 0);
                uint8_t luma_value = 0x00;
                uint8_t uv_value = 0x00;

                if (background_color == background_color::green) {
                    // Background color green: Y=0x00, U=0x00, V=0x00 (green in YUV)
                    luma_value = 0x00;
                    uv_value = 0x00;
                } else {
                    // For other colors, use simple mapping (can be extended later)
                    // For now, use default values
                    luma_value = 0x00;
                    uv_value = 0x80;
                }

                std::memset(init_map_info.data + GST_VIDEO_INFO_PLANE_OFFSET(&video_info_out, 0), luma_value,
                            luma_plane_size);

                const gsize uv_plane_size = GST_VIDEO_INFO_PLANE_STRIDE(&video_info_out, 1) *
                                            GST_VIDEO_INFO_COMP_HEIGHT(&video_info_out, 1);
                std::memset(init_map_info.data + GST_VIDEO_INFO_PLANE_OFFSET(&video_info_out, 1), uv_value,
                            uv_plane_size);

                gst_buffer_unmap(buffer_out.get(), &init_map_info);
            } else {
                gst_buffer_memset(buffer_out.get(), 0, 0, video_info_out.size);
            }
        }

        /**
         * @brief Initializes buffer with specified background color for non-NV12 formats.
         * @param buffer_out Output buffer.
         * @param video_info_out Output video info.
         * @param background_color Background color enum value.
         */
        void initialize_buffer_with_color(unique_gst_buffer_ptr &buffer_out, const GstVideoInfo &video_info_out,
                                          background_color background_color) {
            if (background_color == background_color::green) {
                // For background color green, initialize to zero
                gst_buffer_memset(buffer_out.get(), 0, 0, video_info_out.size);
            } else {
                // For other colors, use simple initialization (can be extended later)
                gst_buffer_memset(buffer_out.get(), 0, 0, video_info_out.size);
            }
        }
    } // namespace

    /**
     * @brief Initializes output buffer based on video format.
     * @param buffer_out Output buffer.
     * @param video_info_out Output video info.
     * @param format Video format.
     * @param background_color Background color enum value (green for YUV formats).
     */
    void initialize_reconstruction_buffer(unique_gst_buffer_ptr &buffer_out, const GstVideoInfo &video_info_out,
                                          const GstVideoFormat format, background_color background_color) {
        if (format == GST_VIDEO_FORMAT_NV12) {
            initialize_nv12_buffer_with_color(buffer_out, video_info_out, background_color);
        } else {
            initialize_buffer_with_color(buffer_out, video_info_out, background_color);
        }
    }
} // namespace sf
