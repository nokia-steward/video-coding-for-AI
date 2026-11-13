// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <optional>
#include <vector>

#include "../bridge/context/memory_policy_runtime.h"
#include "../encoded_frame_item.h"
#include "gst_raii.h"
#include "packed_regions_info.h"
#include "rectangle.h"

/**
 * @file frame_processing_utils.height
 * @brief Declares common utility functions for raw frame processing.
 */

/**
 * @brief Holds properties of a raw video frame.
 */
struct frame_properties {
    int width = 0;
    int height = 0;
    const gchar *format = nullptr;
};

/**
 * @brief Holds the layout of rectangles for a frame.
 */
struct frame_layout {
    std::vector<sf::rectangle> input_rectangles;
    std::vector<sf::rectangle> packed_rectangles;
    std::vector<sf::rectangle> predicted_rectangles;
    std::vector<guint64> input_object_ids; // Object IDs for input rectangles
    std::vector<guint64> predicted_object_ids; // Object IDs for predicted rectangles

    // Default constructor
    frame_layout() :
        input_rectangles(), packed_rectangles(), predicted_rectangles(), input_object_ids(), predicted_object_ids() {}

    // Constructor with parameters
    frame_layout(std::vector<sf::rectangle> input, std::vector<sf::rectangle> packed) :
        input_rectangles(std::move(input)), packed_rectangles(std::move(packed)), predicted_rectangles(),
        input_object_ids(), predicted_object_ids() {}
    // Constructor with all parameters
    frame_layout(std::vector<sf::rectangle> input, std::vector<sf::rectangle> packed,
                 std::vector<sf::rectangle> predicted) :
        input_rectangles(std::move(input)),
        packed_rectangles(std::move(packed)), predicted_rectangles(std::move(predicted)), input_object_ids(),
        predicted_object_ids() {}

    // Constructor with parameters including object IDs
    frame_layout(std::vector<sf::rectangle> input, std::vector<sf::rectangle> packed,
                 std::vector<sf::rectangle> predicted, std::vector<guint64> input_ids,
                 std::vector<guint64> predicted_ids) :
        input_rectangles(std::move(input)),
        packed_rectangles(std::move(packed)), predicted_rectangles(std::move(predicted)),
        input_object_ids(std::move(input_ids)), predicted_object_ids(std::move(predicted_ids)) {}
};

/**
 * @brief Holds normalized source/destination rectangle pairs for copy operations.
 */
struct normalized_copy_rectangles {
    std::vector<sf::rectangle> source_rectangles;
    std::vector<sf::rectangle> destination_rectangles;

    normalized_copy_rectangles() : source_rectangles(), destination_rectangles() {}

    normalized_copy_rectangles(std::vector<sf::rectangle> source, std::vector<sf::rectangle> destination) :
        source_rectangles(std::move(source)), destination_rectangles(std::move(destination)) {}
};

/**
 * @brief Extracts properties like width, height, and format from a GstSample.
 * @param sample The GstSample to inspect (non-owning reference).
 * @return An optional containing the frame properties, or std::nullopt on failure.
 */
[[nodiscard]] std::optional<frame_properties> get_frame_properties(const sf::unique_gst_sample_ptr &sample);

/**
 * @brief Extracts properties like width, height, and format from a GstBuffer.
 * Uses global_data params for width/height and attempts to get format from buffer caps.
 * @param default_width Default width if not found in caps.
 * @param default_height Default height if not found in caps.
 * @return An optional containing the frame properties, or std::nullopt on failure.
 */
[[nodiscard]] std::optional<frame_properties> get_frame_properties_from_buffer(int default_width, int default_height);

/**
 * @brief Creates a new GstBuffer and copies metadata from an existing one.
 * @param input_buffer The source buffer (non-owning reference).
 * @return A unique_ptr to the new buffer, or nullptr on allocation failure.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr create_output_buffer(const sf::unique_gst_buffer_no_delete_ptr &input_buffer);

/**
 * @brief Creates a new GstBuffer with the correct size for video frame properties.
 * @param input_buffer The source buffer (for copying timestamps/flags) (non-owning reference).
 * @param props The frame properties (width, height, format) to determine buffer size.
 * @return A unique_ptr to the new buffer, or nullptr on allocation failure.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr create_output_buffer(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                                             const frame_properties &props);

/**
 * @brief Copies a buffer from a sample using GstVideoFrame API (handles hardware buffers correctly).
 * This creates a system memory copy that will remain valid when the original hardware buffer is unmapped.
 * @param sample The source sample (contains buffer and caps) (non-owning reference).
 * @return A unique_ptr to the copied buffer, or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr copy_buffer_from_sample(const sf::unique_gst_sample_ptr &sample);

/**
 * @brief Copies video frame data from input buffer to output buffer using GstVideoFrame API.
 * This properly handles video format strides and planes.
 * @param input_buffer The source buffer (non-owning reference).
 * @param output_buffer The destination buffer (non-owning reference).
 * @param props The frame properties (width, height, format).
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool copy_video_frame_data(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                         sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                         memory_policy_runtime *memory_runtime = nullptr);

/**
 * @brief Applies multiplicative luma scaling directly on a writable raw-video buffer.
 * @param buffer Buffer containing writable raw frame data.
 * @param props Frame properties (must include width/height/format).
 * @param scale_factor Luma scaling factor in range (0.0, 1.0].
 * @return true when scaling was applied successfully, false otherwise.
 */
[[nodiscard]] bool apply_luma_scaling_to_buffer(sf::unique_gst_buffer_ptr &buffer, const frame_properties &props,
                                                double scale_factor);

/**
 * @brief Applies luma adaptation to a raw buffer and returns the per-frame EOI state.
 *
 * When scaling is disabled or fails, the returned state disables luma-adaptation
 * signaling so downstream processing can safely skip receiver-side backscaling.
 *
 * @param buffer Writable raw-video buffer to update in place.
 * @param props Frame geometry and format describing buffer.
 * @param luma_adaptation_enabled Whether scaling/signaling is configured.
 * @param luma_downscale_factor Configured multiplicative Y-plane scale factor.
 * @return Effective per-frame EOI state for this encoded frame.
 */
[[nodiscard]] encoded_frame_luma_adaptation_state
maybe_apply_luma_adaptation_to_buffer(sf::unique_gst_buffer_ptr &buffer, const frame_properties &props,
                                      bool luma_adaptation_enabled, double luma_downscale_factor);

/**
 * @brief Computes the effective destination rectangle copied by one source/destination pair.
 *
 * The returned rectangle reflects frame-boundary clamping and width/height trimming performed by
 * the copy path.
 *
 * @param source_rectangle Source ROI rectangle.
 * @param destination_rectangle Planned destination ROI rectangle.
 * @param frame_width Frame width in pixels.
 * @param frame_height Frame height in pixels.
 * @return Effective copied destination rectangle, or std::nullopt if nothing is copied.
 */
[[nodiscard]] std::optional<sf::rectangle>
compute_effective_copy_destination(const sf::rectangle &source_rectangle, const sf::rectangle &destination_rectangle,
                                   int frame_width, int frame_height);

/**
 * @brief Normalizes aligned source/destination ROI vectors to effective copy rectangles.
 *
 * Invalid pairs (non-positive effective copy size) are dropped from both vectors to keep them aligned.
 * Destination rectangles are replaced with effective copied destinations.
 *
 * @param source_rectangles Source ROI rectangles.
 * @param destination_rectangles Planned destination ROI rectangles.
 * @param frame_width Frame width in pixels.
 * @param frame_height Frame height in pixels.
 * @return Normalized aligned source/destination vectors for copy + metadata paths.
 */
[[nodiscard]] normalized_copy_rectangles
normalize_copy_rectangle_pairs(const std::vector<sf::rectangle> &source_rectangles,
                               const std::vector<sf::rectangle> &destination_rectangles, int frame_width,
                               int frame_height);

/**
 * @brief Maps buffers and copies pixel data from input ROIs to packed output locations.
 * @param input_buffer The raw input buffer (non-owning reference).
 * @param output_buffer The buffer to write the packed frame to (non-owning reference).
 * @param props The properties of the frame.
 * @param layout The rectangle layout for the frame.
 */
void copy_and_pack_pixel_data(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                              sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                              const frame_layout &layout, uint32_t background_color = 0);

/**
 * @brief Forward declaration for shared application state.
 */
struct global_data;

/**
 * @brief Creates packed regions metadata and processes the frame.
 * @param data The global data structure.
 * @param input_buffer The input buffer.
 * @param output_buffer The output buffer.
 * @param props The frame properties.
 * @param input_rectangles The input rectangles.
 * @param packed_rectangles The packed rectangles.
 * @param background_color Background fill color for uncovered pixels.
 */
void create_metadata_and_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                       sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                       const std::vector<sf::rectangle> &input_rectangles,
                                       const std::vector<sf::rectangle> &packed_rectangles,
                                       uint32_t background_color = 0);


/**
 * @brief Initializes the GStreamer debug category for frame processing utilities.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_frame_processing_utils_debug_category();
