// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "validation.h"

#include <gst/video/video-info.h>
#include <gst/video/video.h>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Creates GstVideoInfo for NV12 format.
         * @param width Frame width.
         * @param height Frame height.
         * @return Optional GstVideoInfo if successful.
         */
        [[nodiscard]] std::optional<GstVideoInfo> create_nv12_video_info(const int width, const int height) {
            GstVideoInfo info;
            gst_video_info_init(&info);
            if (!gst_video_info_set_format(&info, GST_VIDEO_FORMAT_NV12, static_cast<guint>(width),
                                           static_cast<guint>(height))) {
                error(area_copy, "Failed to set video info format for NV12 %dx%d", width, height);
                return std::nullopt;
            }
            return info;
        }

        /**
         * @brief Validates buffer size matches expected format size.
         * @param buffer Buffer to validate.
         * @param expected_size Expected buffer size.
         * @param width Frame width.
         * @param height Frame height.
         * @param buffer_name Buffer name for error messages.
         * @return true if valid, false otherwise.
         */
        [[nodiscard]] bool validate_buffer_size(GstBuffer *buffer, const gsize expected_size, const int width,
                                                const int height, const char *buffer_name) {
            const gsize actual_size = gst_buffer_get_size(buffer);
            if (actual_size < expected_size) {
                error(area_copy, "%s buffer size (%zu) is smaller than expected (%zu) for NV12 %dx%d", buffer_name,
                      actual_size, expected_size, width, height);
                return false;
            }
            return true;
        }
    } // namespace

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
                          GstBuffer *output_buffer) {
        const auto input_info_opt = create_nv12_video_info(input_width, input_height);
        if (!input_info_opt) {
            return std::nullopt;
        }

        const auto output_info_opt = create_nv12_video_info(input_width, input_height);
        if (!output_info_opt) {
            return std::nullopt;
        }

        const GstVideoInfo input_info = *input_info_opt;
        const GstVideoInfo output_info = *output_info_opt;

        if (!validate_buffer_size(const_cast<GstBuffer *>(input_buffer), input_info.size, input_width, input_height,
                                  "Input")) {
            return std::nullopt;
        }

        if (!validate_buffer_size(output_buffer, output_info.size, input_width, input_height, "Output")) {
            return std::nullopt;
        }

        return std::make_pair(input_info, output_info);
    }

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
                                                                                         GstBuffer *output_buffer) {
        GstVideoFrame input_frame;
        GstVideoFrame output_frame;

        if (!gst_video_frame_map(&input_frame, input_info, const_cast<GstBuffer *>(input_buffer), GST_MAP_READ)) {
            error(area_copy, "Failed to map input frame");
            return std::nullopt;
        }

        if (!gst_video_frame_map(&output_frame, output_info, output_buffer, GST_MAP_WRITE)) {
            gst_video_frame_unmap(&input_frame);
            error(area_copy, "Failed to map output frame");
            return std::nullopt;
        }

        return std::make_pair(input_frame, output_frame);
    }
} // namespace sf
