// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/area_copy.h"
#include "../../include/logging.h"
#include "../../include/rectangle.h"
#include "nv12/processing.h"
#include "yuv420/processing.h"

#include <gst/gst.h>
#include <stdexcept>
#include <string_view>
#include <vector>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    /**
     * @brief Packs regions of interest from a full frame into a smaller packed frame.
     *
     * This function is used on the encoding side. It takes a full-resolution raw
     * video frame and copies specified rectangular regions (`input_rectangles`)
     * into a smaller, packed frame at specified locations (`packed_rectangles`).
     *
     * @param in_map_info Mapped GStreamer buffer for the input frame.
     * @param out_map_info Mapped GStreamer buffer for the output frame.
     * @param input_width Width of the input video.
     * @param input_height Height of the input video.
     * @param input_rectangles A vector of rectangles defining the source regions.
     * @param packed_rectangles A vector of rectangles defining the destination regions.
     * @param format A string view of the video format (e.g., "I420").
     *
     * Note: GStreamer MapInfo parameters cannot be const due to GStreamer API requirements.
     * The function only reads from in_map_info and writes to out_map_info.
     */
    void copy_regions_to_packed_frame(const GstMapInfo &in_map_info, const GstMapInfo &out_map_info,
                                      const int input_width, const int input_height,
                                      const std::vector<rectangle> &input_rectangles,
                                      const std::vector<rectangle> &packed_rectangles, video_format format) {
        if (format == video_format::i420 || format == video_format::yuv420) {
            process_yuv420_frame(in_map_info, out_map_info, input_width, input_height, input_rectangles,
                                 packed_rectangles);
        } else {
            throw std::runtime_error(
                    "Unsupported video format for processing: " + std::string(video_format_to_string(format)) +
                    " (NV12 requires GstVideoFrame API - use copy_regions_to_packed_frame_nv12)");
        }
    }

    /**
     * @brief Packs regions of interest from a full NV12 frame into a smaller packed frame using GstVideoFrame API.
     *
     * This function is used on the encoding side for NV12 format. It uses GstVideoFrame API
     * for safe stride/memory handling with hardware buffers.
     *
     * @param input_buffer Input GStreamer buffer for the input frame.
     * @param output_buffer Output GStreamer buffer for the output frame.
     * @param input_width Width of the input video.
     * @param input_height Height of the input video.
     * @param input_rectangles A vector of rectangles defining the source regions.
     * @param packed_rectangles A vector of rectangles defining the destination regions.
     */
    void copy_regions_to_packed_frame_nv12(const unique_gst_buffer_ptr &input_buffer,
                                           unique_gst_buffer_ptr &output_buffer, const int input_width,
                                           const int input_height, const std::vector<rectangle> &input_rectangles,
                                           const std::vector<rectangle> &packed_rectangles) {
        process_nv12_frame(input_buffer.get(), output_buffer.get(), input_width, input_height, input_rectangles,
                           packed_rectangles);
    }

    /**
     * @brief Packs regions of interest from a full frame into a smaller packed frame with improved error handling.
     *
     * This function is used on the encoding side. It takes a full-resolution raw
     * video frame and copies specified rectangular regions (`input_rectangles`)
     * into a smaller, packed frame at specified locations (`packed_rectangles`).
     *
     * @param in_map_info Mapped GStreamer buffer for the input frame.
     * @param out_map_info Mapped GStreamer buffer for the output frame.
     * @param input_width Width of the input video.
     * @param input_height Height of the input video.
     * @param input_rectangles A vector of rectangles defining the source regions.
     * @param packed_rectangles A vector of rectangles defining the destination regions.
     * @param format A string view of the video format (e.g., "I420").
     * @return A copy_result indicating success or failure with error details.
     *
     * Note: GStreamer MapInfo parameters cannot be const due to GStreamer API requirements.
     * The function only reads from in_map_info and writes to out_map_info.
     */
    [[nodiscard]] copy_result copy_regions_to_packed_frame_safe(const GstMapInfo &in_map_info,
                                                                const GstMapInfo &out_map_info, const int input_width,
                                                                const int input_height,
                                                                const std::vector<rectangle> &input_rectangles,
                                                                const std::vector<rectangle> &packed_rectangles,
                                                                video_format format) {
        if (input_width <= 0 || input_height <= 0) {
            return copy_result::failure(copy_error::invalid_dimensions, "Invalid input dimensions");
        }

        if (input_rectangles.size() != packed_rectangles.size()) {
            return copy_result::failure(copy_error::invalid_dimensions, "Mismatched rectangle counts");
        }

        if (format != video_format::i420 && format != video_format::yuv420) {
            return copy_result::failure(copy_error::unsupported_format,
                                        "Unsupported video format: " + std::string(video_format_to_string(format)));
        }

        try {
            process_yuv420_frame(in_map_info, out_map_info, input_width, input_height, input_rectangles,
                                 packed_rectangles);
            return copy_result::success();
        } catch (const std::out_of_range &e) {
            return copy_result::failure(copy_error::out_of_bounds, e.what());
        } catch (const std::exception &e) {
            return copy_result::failure(copy_error::buffer_error, e.what());
        }
    }
} // namespace sf
