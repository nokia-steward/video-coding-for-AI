// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <string>

#include "gst/video/video-info.h"
#include "gst_raii.h"
#include "rectangle.h"

namespace sf {
    /**
     * @file gst_wrappers.h
     * @brief Clean C++ wrappers around GStreamer C APIs to isolate impurity.
     *
     * This module provides functional-style wrappers around GStreamer APIs,
     * isolating the impure C-style operations and providing clean, testable interfaces.
     */

    /**
     * @brief Result type for GStreamer operations.
     */
    struct gst_result {
        bool success;
        std::string error_message;

        /**
         * @brief Create a success result.
         * @return gst_result representing success.
         */
        [[nodiscard]] static gst_result ok() { return {true, ""}; }

        /**
         * @brief Create a failure result with an error message.
         * @param msg Error message to report.
         * @return gst_result representing failure.
         */
        [[nodiscard]] static gst_result error(const std::string &msg) { return {false, msg}; }
    };

    /**
     * @brief Pure function to validate video dimensions.
     * @param width The width to validate.
     * @param height The height to validate.
     * @return true if dimensions are valid, false otherwise.
     */
    [[nodiscard]] constexpr bool is_valid_video_dimensions(const int width, const int height) {
        return width > 0 && height > 0 && width <= 8192 && height <= 8192;
    }

    /**
     * @brief Pure function to calculate YUV420 plane sizes.
     * @param width The width of the video.
     * @param height The height of the video.
     * @return A struct containing the calculated sizes.
     */
    struct yuv420_plane_sizes {
        size_t luma_size;
        size_t chroma_u_size;
        size_t chroma_v_size;
        size_t total_size;
    };

    /**
     * @brief Calculate plane sizes for a YUV420 buffer.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @return Computed plane sizes.
     */
    [[nodiscard]] constexpr yuv420_plane_sizes calculate_yuv420_plane_sizes(const int width, const int height) {
        const size_t luma_size = static_cast<size_t>(width * height);
        const size_t chroma_u_size = luma_size / 4;
        const size_t chroma_v_size = luma_size / 4;
        const size_t total_size = luma_size + chroma_u_size + chroma_v_size;
        return {luma_size, chroma_u_size, chroma_v_size, total_size};
    }

    /**
     * @brief Pure function to validate rectangle bounds.
     * @param rect The rectangle to validate.
     * @param frame_width The width of the frame.
     * @param frame_height The height of the frame.
     * @return true if rectangle is within bounds, false otherwise.
     */
    [[nodiscard]] constexpr bool is_rectangle_within_bounds(const rectangle &rect, const int frame_width,
                                                            const int frame_height) {
        return rect.left >= 0 && rect.top >= 0 && rect.left + rect.width <= frame_width &&
               rect.top + rect.height <= frame_height && rect.width > 0 && rect.height > 0;
    }

    /**
     * @brief Wrapper for GStreamer buffer mapping with RAII.
     * @param buffer The GstBuffer to map.
     * @param map_flags The mapping flags.
     * @return A unique_ptr to the mapped buffer info, or nullptr on failure.
     */
    [[nodiscard]] std::unique_ptr<GstMapInfo> map_buffer_safe(GstBuffer *buffer, GstMapFlags map_flags);

    /**
     * @brief Wrapper for GStreamer video info creation.
     * @param caps The GstCaps to create video info from.
     * @return A unique_ptr to the video info, or nullptr on failure.
     */
    [[nodiscard]] std::unique_ptr<GstVideoInfo> create_video_info_safe(GstCaps *caps);

    /**
     * @brief Wrapper for GStreamer sample creation.
     * @param buffer The GstBuffer for the sample.
     * @param caps The GstCaps for the sample.
     * @param segment The GstSegment for the sample.
     * @param info The GstStructure for the sample.
     * @return A unique_ptr to the sample, or nullptr on failure.
     */
    [[nodiscard]] unique_gst_sample_ptr create_sample_safe(GstBuffer *buffer, GstCaps *caps,
                                                           GstSegment *segment = nullptr, GstStructure *info = nullptr);

    /**
     * @brief Pure function to extract timing information from a buffer.
     * @param buffer The GstBuffer to extract timing from.
     * @return A struct containing the timing information.
     */
    struct buffer_timing {
        GstClockTime pts;
        GstClockTime dts;
        GstClockTime duration;
    };

    /**
     * @brief Extract timing metadata from a buffer.
     * @param buffer Buffer to read timing data from.
     * @return Extracted buffer timing values.
     */
    [[nodiscard]] inline buffer_timing extract_buffer_timing(const unique_gst_buffer_ptr &buffer) {
        return {GST_BUFFER_PTS(buffer.get()), GST_BUFFER_DTS(buffer.get()), GST_BUFFER_DURATION(buffer.get())};
    }

    /**
     * @brief Pure function to set timing information on a buffer.
     * @param buffer The GstBuffer to set timing on (non-owning reference).
     * @param timing The timing information to set.
     */
    void set_buffer_timing(unique_gst_buffer_ptr &buffer, const buffer_timing &timing);

} // namespace sf
