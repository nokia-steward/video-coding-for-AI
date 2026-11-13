// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <gst/gstmemory.h>
#include <string_view>
#include <vector>

#include "gst_raii.h"
#include "packed_regions_info.h"
#include "rectangle.h"

namespace sf {
    /**
     * @file area_copy.h
     * @brief Provides functions for copying regions within raw video frames.
     *
     * This module follows functional programming principles:
     * - Pure data transformations: functions operate on input data to produce output
     * - Immutable parameters: all input parameters are const references
     * - Clear separation of concerns: each function has a single responsibility
     * - Error handling through return values: functions return success/failure status
     */

    /**
     * @enum video_format
     * @brief Defines supported video formats.
     */
    enum class video_format { i420, nv12, yuv420 };

    /**
     * @enum roi_detection_mode
     * @brief Defines ROI detection modes.
     */
    enum class roi_detection_mode { inference, static_mode, none };

    /**
     * @enum background_color
     * @brief Defines background colors for empty frame areas.
     */
    enum class background_color : uint32_t { green = 0 };

    /**
     * @brief Converts video_format enum to GStreamer format string.
     * @param format The video format enum value.
     * @return GStreamer format string (e.g., "I420", "NV12").
     */
    [[nodiscard]] const char *video_format_to_string(video_format format);

    /**
     * @brief Converts string to video_format enum.
     * @param format_str The format string (e.g., "I420", "NV12", "YUV420").
     * @return The video_format enum value, or i420 if string is not recognized.
     */
    [[nodiscard]] video_format string_to_video_format(const char *format_str);

    /**
     * @enum copy_error
     * @brief Error types for region copying operations.
     */
    enum class copy_error { success, invalid_dimensions, out_of_bounds, unsupported_format, buffer_error };

    /**
     * @brief Result type for region copying operations.
     */
    struct copy_result {
        copy_error error;
        std::string message;

        /**
         * @brief Check whether the copy operation succeeded.
         * @return true if error is copy_error::success.
         */
        [[nodiscard]] bool is_success() const { return error == copy_error::success; }

        /**
         * @brief Create a success result.
         * @return copy_result representing success.
         */
        [[nodiscard]] static copy_result success() { return {copy_error::success, ""}; }

        /**
         * @brief Create a failure result with error details.
         * @param err Error code describing the failure.
         * @param msg Human-readable error message.
         * @return copy_result representing failure.
         */
        [[nodiscard]] static copy_result failure(const copy_error err, const std::string &msg) { return {err, msg}; }
    };

    /**
     * @struct yuv_420_plane_pointers
     * @brief Holds pointers to the Y, U, and V planes for both input and output buffers.
     */
    struct yuv_420_plane_pointers {
        const uint8_t *y_in;
        const uint8_t *u_in;
        const uint8_t *v_in;
        uint8_t *y_out;
        uint8_t *u_out;
        uint8_t *v_out;
    };

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
     * @param format The video format enum value.
     *
     * Note: GStreamer MapInfo parameters cannot be const due to GStreamer API requirements.
     * The function only reads from in_map_info and writes to out_map_info.
     */
    void copy_regions_to_packed_frame(const GstMapInfo &in_map_info, const GstMapInfo &out_map_info, int input_width,
                                      int input_height, const std::vector<rectangle> &input_rectangles,
                                      const std::vector<rectangle> &packed_rectangles, video_format format);

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
     * @param format The video format enum value.
     * @return A copy_result indicating success or failure with error details.
     *     * Note: GStreamer MapInfo parameters cannot be const due to GStreamer API requirements.
     * The function only reads from in_map_info and writes to out_map_info.
     */
    [[nodiscard]] copy_result
    copy_regions_to_packed_frame_safe(const GstMapInfo &in_map_info, const GstMapInfo &out_map_info, int input_width,
                                      int input_height, const std::vector<rectangle> &input_rectangles,
                                      const std::vector<rectangle> &packed_rectangles, video_format format);

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
                                           unique_gst_buffer_ptr &output_buffer, int input_width, int input_height,
                                           const std::vector<rectangle> &input_rectangles,
                                           const std::vector<rectangle> &packed_rectangles);

    /**
     * @brief Reconstructs a full frame from a packed frame using PRI SEI metadata.
     *
     * This function is used on the decoding side. It takes a packed video frame
     * and the corresponding Packed Regions Information (PRI) and reconstructs the
     * original, full-resolution frame by copying the regions to their target
     * locations.
     *
     * @param packed_sample The GstSample containing the packed frame.
     * @param pri_info The PRI metadata describing how to reconstruct the frame.
     * @param background_color Background color for empty areas (default: green).
     * @return A new GstSample containing the reconstructed full frame, or nullptr on failure.
     *
     * Note: The packed_sample parameter is moved from (rvalue reference) to take ownership.
     * The function creates a new GstSample and transfers ownership to the caller.
     */
    [[nodiscard]] unique_gst_sample_ptr reconstruct_frame(unique_gst_sample_ptr &&packed_sample,
                                                          const packed_regions_info &pri_info,
                                                          background_color background_color = background_color::green);

    /**
     * @brief Creates an empty buffer with the specified dimensions and format.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Video format enum value (default: i420).
     * @param background_color Background color for empty areas (default: green).
     * @return Unique pointer to the created buffer, or nullptr on failure.
     */
    [[nodiscard]] unique_gst_buffer_ptr
    create_empty_buffer(int width, int height, video_format format = video_format::i420,
                        background_color background_color = background_color::green);

    /**
     * @brief Ensures that a rectangle has even dimensions and coordinates for YUV420 format compatibility.
     *
     * YUV420 format requires even widths, heights, and coordinates for proper UV plane subsampling.
     * This function uses a safe compression strategy to ensure the rectangle stays within frame bounds.
     *
     * @param rect The rectangle to validate and correct.
     * @param frame_width The width of the frame (for bounds checking).
     * @param frame_height The height of the frame (for bounds checking).
     * @return A corrected rectangle with even dimensions and coordinates that fits within frame bounds.
     */
    [[nodiscard]] rectangle ensure_even_dimensions(const rectangle &rect, int frame_width, int frame_height);

    /**
     * @brief Ensures that a rectangle has even dimensions and coordinates for YUV420 format compatibility.
     *
     * This is a simplified version that doesn't check frame bounds. Use with caution.
     * Prefer the version with frame bounds for safety.
     *
     * @param rect The rectangle to validate and correct.
     * @return A corrected rectangle with even dimensions and coordinates.
     */
    [[nodiscard]] rectangle ensure_even_dimensions(const rectangle &rect);

    /**
     * @brief Validates that a rectangle has even dimensions and coordinates for YUV420 format.
     *
     * @param rect The rectangle to validate.
     * @return true if the rectangle has even dimensions and coordinates, false otherwise.
     */
    [[nodiscard]] bool has_even_dimensions(const rectangle &rect);
} // namespace sf

/**
 * @brief Initialize the area_copy debug category.
 * This function should be called after gst_init() to ensure the category is properly initialized.
 */
void init_area_copy_debug_category();
