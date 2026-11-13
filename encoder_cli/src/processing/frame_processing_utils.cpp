// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "frame_processing_utils.h"
#include "../bridge/app/pipeline_app_adapter.h"
#include "../global_data.h"
#include "../gst/input_types/frame_path_meta.h"
#include "../utils/packed_regions_info_utils.h"
#include "area_copy.h"
#include "gst_raii.h"
#include "memory_contract_helper.h"
#include "packed_regions_info.h"
#include "roi_detection_packing.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string_view>
#include "../../libvcai/include/logging.h"

/**
 * @file frame_processing_utils.cpp
 * @brief Implementation of common utility functions for raw frame processing.
 */

/// GStreamer debug category for frame processing utilities.
GST_DEBUG_CATEGORY_STATIC(frame_processing_utils);

void init_frame_processing_utils_debug_category() {
    GST_DEBUG_CATEGORY_INIT(frame_processing_utils, "frame-processing-utils", 0,
                            "Utility functions for frame processing operations");
}

namespace {
    /**
     * @brief Copies video frame data using GstVideoInfo-derived layout details.
     * @param input_buffer Source buffer to read from.
     * @param output_buffer Destination buffer to write into.
     * @param video_info GstVideoInfo describing format and layout.
     * @param required_size Minimum expected size for output buffer.
     * @return True if the copy succeeds, false otherwise.
     */
    bool copy_video_frame_data_impl(GstBuffer *input_buffer, GstBuffer *output_buffer, const GstVideoInfo *video_info,
                                    gsize required_size);

    /**
     * @brief Structure to hold buffer timestamp and flag fields atomically.
     * This ensures thread-safe reading of buffer fields.
     */
    struct buffer_fields {
        GstClockTime pts;
        GstClockTime dts;
        GstClockTime duration;
        guint64 offset;
        guint64 offset_end;
        guint flags; // GST_BUFFER_FLAGS returns guint, not GstBufferFlags
    };

    /**
     * @brief Reads all buffer fields atomically for thread-safety.
     * @param buffer The buffer to read fields from.
     * @return buffer_fields structure containing all fields.
     */
    [[nodiscard]] buffer_fields read_buffer_fields_atomic(GstBuffer *buffer) {
        return {.pts = GST_BUFFER_PTS(buffer),
                .dts = GST_BUFFER_DTS(buffer),
                .duration = GST_BUFFER_DURATION(buffer),
                .offset = GST_BUFFER_OFFSET(buffer),
                .offset_end = GST_BUFFER_OFFSET_END(buffer),
                .flags = GST_BUFFER_FLAGS(buffer)};
    }

    /**
     * @brief Copies buffer fields to output buffer.
     * @param output_buffer The destination buffer.
     * @param fields The fields to copy.
     */
    void copy_buffer_fields(GstBuffer *output_buffer, const buffer_fields &fields) {
        GST_BUFFER_PTS(output_buffer) = fields.pts;
        GST_BUFFER_DTS(output_buffer) = fields.dts;
        GST_BUFFER_DURATION(output_buffer) = fields.duration;
        GST_BUFFER_OFFSET(output_buffer) = fields.offset;
        GST_BUFFER_OFFSET_END(output_buffer) = fields.offset_end;
        GST_BUFFER_FLAGS(output_buffer) = fields.flags;
    }


} // namespace

/**
 * @brief Extracts frame properties (width, height, format) from a GstSample.
 * @param sample The GstSample to inspect.
 * @return An optional containing the frame_properties struct, or std::nullopt on failure.
 */
std::optional<frame_properties> get_frame_properties(const sf::unique_gst_sample_ptr &sample) {
    const auto *caps = gst_sample_get_caps(sample.get());
    if (!caps) {
        sf::error(frame_processing_utils, "Failed to get caps from sample.");
        return std::nullopt;
    }
    const auto *structure = gst_caps_get_structure(caps, 0);
    if (!structure) {
        sf::error(frame_processing_utils, "Failed to get structure from caps.");
        return std::nullopt;
    }

    frame_properties props{};
    if (!gst_structure_get_int(structure, "width", &props.width) ||
        !gst_structure_get_int(structure, "height", &props.height)) {
        sf::error(frame_processing_utils, "Failed to get width/height from caps structure.");
        return std::nullopt;
    }

    props.format = gst_structure_get_string(structure, "format");
    if (!props.format) {
        sf::error(frame_processing_utils, "Failed to get format from caps structure.");
        return std::nullopt;
    }

    return props;
}

/**
 * @brief Derives frame properties from buffer caps or defaults.
 * @param default_width Default width if caps are missing.
 * @param default_height Default height if caps are missing.
 * @return Frame properties if available, otherwise nullopt.
 */
std::optional<frame_properties> get_frame_properties_from_buffer(const int default_width, const int default_height) {
    frame_properties props{};
    props.width = default_width;
    props.height = default_height;
    props.format = "NV12"; // Default format for DeepStream (buffered frames are raw video) - keep as string for
                           // GStreamer compatibility

    return props;
}

/**
 * @brief Creates a new GstBuffer for output, copying metadata from an input buffer.
 * @param input_buffer The original GstBuffer to copy metadata from.
 * @return A unique_gst_buffer_ptr to the newly created buffer, or nullptr on failure.
 */
sf::unique_gst_buffer_ptr create_output_buffer(const sf::unique_gst_buffer_no_delete_ptr &input_buffer) {
    auto output_buffer = sf::unique_gst_buffer_ptr(
            gst_buffer_new_allocate(nullptr, gst_buffer_get_size(input_buffer.get()), nullptr));
    if (!output_buffer) {
        sf::error(frame_processing_utils, "Failed to allocate output buffer.");
        return nullptr;
    }

    // Note: We can't determine format from buffer alone, so we'll initialize in copy_and_pack_pixel_data
    // For now, just zero the buffer as a safe default
    gst_buffer_memset(output_buffer.get(), 0, 0, gst_buffer_get_size(output_buffer.get()));

    // Copy only timestamps and flags, NOT metadata structures
    // Metadata may contain size-dependent information that would be invalid
    // Read fields atomically for thread-safety
    const auto fields = read_buffer_fields_atomic(input_buffer.get());
    copy_buffer_fields(output_buffer.get(), fields);

    return output_buffer;
}

sf::unique_gst_buffer_ptr create_output_buffer(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                               const frame_properties &props) {
    if (!props.format) {
        sf::error(frame_processing_utils, "Cannot create output buffer without format information");
        return create_output_buffer(input_buffer); // Fallback to size-based allocation
    }

    // Create caps from frame properties
    const sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, props.format,
                                                           "width", G_TYPE_INT, props.width, "height", G_TYPE_INT,
                                                           props.height, nullptr));
    if (!caps) {
        sf::error(frame_processing_utils, "Failed to create caps for output buffer");
        return create_output_buffer(input_buffer); // Fallback to size-based allocation
    }

    // Parse video info from caps to get correct buffer size
    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, caps.get())) {
        sf::error(frame_processing_utils, "Failed to parse video info from caps");
        return create_output_buffer(input_buffer); // Fallback to size-based allocation
    }

    // Allocate buffer with correct size
    auto output_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, video_info.size, nullptr));
    if (!output_buffer) {
        sf::error(frame_processing_utils, "Failed to allocate output buffer with correct size.");
        return nullptr;
    }

    // Zero the buffer
    gst_buffer_memset(output_buffer.get(), 0, 0, video_info.size);

    // Copy only timestamps and flags, NOT metadata structures
    // Metadata may contain size-dependent information that would be invalid
    // Read fields atomically for thread-safety
    const auto fields = read_buffer_fields_atomic(input_buffer.get());
    copy_buffer_fields(output_buffer.get(), fields);

    return output_buffer;
}

/**
 * @brief Copies a buffer from a sample using GstVideoFrame API (handles hardware buffers correctly).
 * This creates a system memory copy that will remain valid when the original hardware buffer is unmapped.
 */
sf::unique_gst_buffer_ptr copy_buffer_from_sample(const sf::unique_gst_sample_ptr &sample) {
    if (!sample) {
        sf::error(frame_processing_utils, "Cannot copy buffer from null sample");
        return nullptr;
    }

    auto *input_buffer = gst_sample_get_buffer(sample.get());
    const auto *caps = gst_sample_get_caps(sample.get());
    if (!input_buffer || !caps) {
        sf::error(frame_processing_utils, "Sample is missing buffer or caps");
        return nullptr;
    }

    // Parse video info from caps
    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, caps)) {
        sf::error(frame_processing_utils, "Failed to parse video info from caps");
        return nullptr;
    }

    // Allocate new system memory buffer
    auto output_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, video_info.size, nullptr));
    if (!output_buffer) {
        sf::error(frame_processing_utils, "Failed to allocate output buffer");
        return nullptr;
    }

    // Use GstVideoFrame API to copy (handles hardware buffers correctly)
    GstVideoFrame input_frame;
    GstVideoFrame output_frame;

    if (!gst_video_frame_map(&input_frame, &video_info, input_buffer, GST_MAP_READ)) {
        sf::error(frame_processing_utils, "Failed to map input frame");
        return nullptr;
    }

    if (!gst_video_frame_map(&output_frame, &video_info, output_buffer.get(), GST_MAP_WRITE)) {
        gst_video_frame_unmap(&input_frame);
        sf::error(frame_processing_utils, "Failed to map output frame");
        return nullptr;
    }

    // Copy all planes
    const guint num_planes = GST_VIDEO_INFO_N_PLANES(&video_info);
    for (guint plane = 0; plane < num_planes; ++plane) {
        const gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&video_info, plane);
        const gint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE(&video_info, plane);
        const gint comp_width = GST_VIDEO_INFO_COMP_WIDTH(&video_info, plane);
        const gint pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&video_info, plane);
        const gsize row_size = static_cast<gsize>(comp_width) * static_cast<gsize>(pixel_stride);

        const auto *src_plane = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&input_frame, plane));
        auto *dst_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, plane));

        for (gint row = 0; row < plane_height; ++row) {
            std::memcpy(dst_plane + static_cast<gsize>(row) * static_cast<gsize>(plane_stride),
                        src_plane + static_cast<gsize>(row) * static_cast<gsize>(plane_stride), row_size);
        }
    }

    // Unmap frames
    gst_video_frame_unmap(&output_frame);
    gst_video_frame_unmap(&input_frame);

    // Copy timestamps and flags only, NOT metadata structures
    // Metadata may contain size-dependent information that would be invalid
    // Read fields atomically for thread-safety
    const auto fields = read_buffer_fields_atomic(input_buffer);
    copy_buffer_fields(output_buffer.get(), fields);

    return output_buffer;
}

/**
 * @brief Copies video frame data from input buffer to output buffer using GstVideoFrame API.
 * This properly handles video format strides and planes.
 * @param input_buffer The source buffer.
 * @param output_buffer The destination buffer.
 * @param props The frame properties (width, height, format).
 * @return true if successful, false otherwise.
 */
bool copy_video_frame_data(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                           sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                           memory_policy_runtime *memory_runtime) {
    if (!input_buffer || !output_buffer || !props.format) {
        sf::error(frame_processing_utils, "Invalid parameters for copy_video_frame_data");
        return false;
    }

    // Create caps from frame properties
    const sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, props.format,
                                                           "width", G_TYPE_INT, props.width, "height", G_TYPE_INT,
                                                           props.height, nullptr));
    if (!caps) {
        sf::error(frame_processing_utils, "Failed to create caps for video frame copy");
        return false;
    }

    // Parse video info from caps
    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, caps.get())) {
        sf::error(frame_processing_utils, "Failed to parse video info from caps");
        return false;
    }

    // Validate output buffer size is at least the required size
    // Buffer can be larger due to alignment/padding, but must be at least the required size
    const gsize required_size = video_info.size;
    const gsize current_size = gst_buffer_get_size(output_buffer.get());
    if (current_size < required_size) {
        sf::error(frame_processing_utils,
                  "Output buffer size (%zu) is smaller than required video frame size (%zu). "
                  "Buffer should be created with create_output_buffer(input_buffer, props) to ensure correct size.",
                  current_size, required_size);
        return false;
    }
    // Note: Buffer can be larger than required_size - that's fine, we'll only use the required amount

    // Try to map the input buffer first - if it fails, it's likely NVMM or hardware memory
    // For NVMM buffers, gst_video_frame_map will fail with "invalid buffer size"
    GstVideoFrame test_frame;
    if (gst_video_frame_map(&test_frame, &video_info, input_buffer.get(), GST_MAP_READ)) {
        // Buffer can be mapped directly - use standard approach
        gst_video_frame_unmap(&test_frame);
        return copy_video_frame_data_impl(input_buffer.get(), output_buffer.get(), &video_info, required_size);
    }

    if (memory_runtime == nullptr) {
        // Buffer can't be mapped directly - likely NVMM or hardware memory.
        // Without runtime policy context we cannot account for controlled fallback.
        sf::error(frame_processing_utils, "Input buffer cannot be mapped (likely NVMM/hardware memory). "
                                          "NVMM buffers need to be converted to system memory in the pipeline.");
        return false;
    }

    auto fallback_copy = create_system_memory_fallback_copy(input_buffer.get(), *memory_runtime, frame_processing_utils,
                                                            "frame_processing_utils.copy_video_frame_data");
    if (!fallback_copy) {
        return false;
    }

    if (!gst_video_frame_map(&test_frame, &video_info, fallback_copy.get(), GST_MAP_READ)) {
        memory_runtime->record_fallback_failure();
        sf::error(frame_processing_utils,
                  "Memory fallback copy is still not mappable for video frame processing; dropping frame");
        return false;
    }
    gst_video_frame_unmap(&test_frame);

    if (copy_video_frame_data_impl(fallback_copy.get(), output_buffer.get(), &video_info, required_size)) {
        return true;
    }

    memory_runtime->record_fallback_failure();
    sf::error(frame_processing_utils, "Video frame copy failed after memory-domain fallback conversion");
    return false;
}

bool apply_luma_scaling_to_buffer(sf::unique_gst_buffer_ptr &buffer, const frame_properties &props,
                                  const double scale_factor) {
    if (!buffer || !props.format) {
        sf::error(frame_processing_utils, "Invalid parameters for luma scaling");
        return false;
    }
    if (scale_factor <= 0.0 || scale_factor > 1.0) {
        sf::error(frame_processing_utils, "Invalid luma scale factor %.6f (expected (0.0, 1.0])", scale_factor);
        return false;
    }
    if (std::abs(scale_factor - 1.0) < 1e-9) {
        return true;
    }

    const sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, props.format,
                                                           "width", G_TYPE_INT, props.width, "height", G_TYPE_INT,
                                                           props.height, nullptr));
    if (!caps) {
        sf::error(frame_processing_utils, "Failed to create caps for luma scaling");
        return false;
    }

    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, caps.get())) {
        sf::error(frame_processing_utils, "Failed to parse video info for luma scaling");
        return false;
    }

    GstVideoFrame frame;
    if (!gst_video_frame_map(&frame, &video_info, buffer.get(), GST_MAP_READWRITE)) {
        sf::error(frame_processing_utils, "Failed to map frame for luma scaling");
        return false;
    }

    auto *luma_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    if (!luma_plane) {
        gst_video_frame_unmap(&frame);
        sf::error(frame_processing_utils, "Missing luma plane while applying luma scaling");
        return false;
    }

    const gint luma_height = GST_VIDEO_INFO_COMP_HEIGHT(&video_info, 0);
    const gint luma_width = GST_VIDEO_INFO_COMP_WIDTH(&video_info, 0);
    const gint luma_stride = GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0);
    const gint pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&video_info, 0);

    for (gint row = 0; row < luma_height; ++row) {
        auto *row_ptr = luma_plane + static_cast<gsize>(row) * static_cast<gsize>(luma_stride);
        for (gint col = 0; col < luma_width; ++col) {
            const gsize idx = static_cast<gsize>(col) * static_cast<gsize>(pixel_stride);
            const auto original = static_cast<double>(row_ptr[idx]);
            const auto scaled = static_cast<int>(std::floor(original * scale_factor));
            row_ptr[idx] = static_cast<uint8_t>(std::clamp(scaled, 0, 255));
        }
    }

    gst_video_frame_unmap(&frame);
    return true;
}

encoded_frame_luma_adaptation_state maybe_apply_luma_adaptation_to_buffer(sf::unique_gst_buffer_ptr &buffer,
                                                                          const frame_properties &props,
                                                                          const bool luma_adaptation_enabled,
                                                                          const double luma_downscale_factor) {
    if (!luma_adaptation_enabled) {
        return {};
    }

    if (!apply_luma_scaling_to_buffer(buffer, props, luma_downscale_factor)) {
        return {};
    }

    return {.enabled = true, .downscale_factor = luma_downscale_factor};
}

std::optional<sf::rectangle> compute_effective_copy_destination(const sf::rectangle &source_rectangle,
                                                                const sf::rectangle &destination_rectangle,
                                                                const int frame_width, const int frame_height) {
    if (frame_width <= 0 || frame_height <= 0) {
        return std::nullopt;
    }

    const int source_x = std::max(0, source_rectangle.left);
    const int source_y = std::max(0, source_rectangle.top);
    const int source_w = std::min(source_rectangle.width, frame_width - source_x);
    const int source_h = std::min(source_rectangle.height, frame_height - source_y);

    const int destination_x = std::max(0, destination_rectangle.left);
    const int destination_y = std::max(0, destination_rectangle.top);
    const int destination_w = std::min(destination_rectangle.width, frame_width - destination_x);
    const int destination_h = std::min(destination_rectangle.height, frame_height - destination_y);

    const int copy_w = std::min(source_w, destination_w);
    const int copy_h = std::min(source_h, destination_h);
    if (copy_w <= 0 || copy_h <= 0) {
        return std::nullopt;
    }

    return sf::rectangle(destination_x, destination_y, copy_w, copy_h);
}

normalized_copy_rectangles normalize_copy_rectangle_pairs(const std::vector<sf::rectangle> &source_rectangles,
                                                          const std::vector<sf::rectangle> &destination_rectangles,
                                                          const int frame_width, const int frame_height) {
    normalized_copy_rectangles normalized;
    const size_t pair_count = std::min(source_rectangles.size(), destination_rectangles.size());
    normalized.source_rectangles.reserve(pair_count);
    normalized.destination_rectangles.reserve(pair_count);

    for (size_t index = 0; index < pair_count; ++index) {
        const auto effective_destination = compute_effective_copy_destination(
                source_rectangles[index], destination_rectangles[index], frame_width, frame_height);
        if (!effective_destination.has_value()) {
            continue;
        }

        sf::rectangle normalized_source = source_rectangles[index];
        normalized_source.left = std::max(0, normalized_source.left);
        normalized_source.top = std::max(0, normalized_source.top);
        normalized_source.width = std::min(normalized_source.width, frame_width - normalized_source.left);
        normalized_source.height = std::min(normalized_source.height, frame_height - normalized_source.top);
        normalized_source.width = std::min(normalized_source.width, effective_destination->width);
        normalized_source.height = std::min(normalized_source.height, effective_destination->height);

        if (normalized_source.width <= 0 || normalized_source.height <= 0) {
            continue;
        }

        normalized.source_rectangles.push_back(normalized_source);
        normalized.destination_rectangles.push_back(*effective_destination);
    }

    return normalized;
}

namespace {
    /**
     * @brief Internal implementation of video frame data copying.
     * @param input_buffer The source buffer (non-owning).
     * @param output_buffer The destination buffer (non-owning).
     * @param video_info The video info structure.
     * @param required_size The required buffer size (unused, kept for API consistency).
     * @return true if successful, false otherwise.
     */
    bool copy_video_frame_data_impl(GstBuffer *input_buffer, GstBuffer *output_buffer, const GstVideoInfo *video_info,
                                    gsize /* required_size */) {
        GstVideoFrame input_frame;
        GstVideoFrame output_frame;

        if (!gst_video_frame_map(&input_frame, video_info, input_buffer, GST_MAP_READ)) {
            sf::error(frame_processing_utils, "Failed to map input frame for reading");
            return false;
        }

        if (!gst_video_frame_map(&output_frame, video_info, output_buffer, GST_MAP_WRITE)) {
            gst_video_frame_unmap(&input_frame);
            sf::error(frame_processing_utils, "Failed to map output frame for writing");
            return false;
        }

        // Copy all planes
        const guint num_planes = GST_VIDEO_INFO_N_PLANES(video_info);
        for (guint plane = 0; plane < num_planes; ++plane) {
            const gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, plane);
            const gint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE(video_info, plane);
            const gint comp_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, plane);
            const gint pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(video_info, plane);
            const gsize row_size = static_cast<gsize>(comp_width) * static_cast<gsize>(pixel_stride);

            const auto *src_plane = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&input_frame, plane));
            auto *dst_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, plane));

            for (gint row = 0; row < plane_height; ++row) {
                std::memcpy(dst_plane + static_cast<gsize>(row) * static_cast<gsize>(plane_stride),
                            src_plane + static_cast<gsize>(row) * static_cast<gsize>(plane_stride), row_size);
            }
        }

        // Unmap frames
        gst_video_frame_unmap(&output_frame);
        gst_video_frame_unmap(&input_frame);

        return true;
    }
} // namespace

/**
 * @brief Copies and packs pixel data from an input buffer to an output buffer based on a given layout.
 * @param input_buffer The buffer containing the original frame data.
 * @param output_buffer The buffer to write the packed frame data to.
 * @param props The properties of the frame (width, height, format).
 * @param layout The layout defining which rectangles to copy and where to place them.
 */
void copy_and_pack_pixel_data(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                              sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                              const frame_layout &layout, uint32_t background_color) {
    // Validate buffer sizes before copying
    if (!props.format) {
        sf::error(frame_processing_utils, "Cannot copy pixel data without format information");
        return;
    }

    // Create caps from frame properties to calculate required buffer size
    const sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, props.format,
                                                           "width", G_TYPE_INT, props.width, "height", G_TYPE_INT,
                                                           props.height, nullptr));
    if (!caps) {
        sf::error(frame_processing_utils, "Failed to create caps for buffer size validation");
        return;
    }

    // Parse video info from caps to get correct buffer size
    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, caps.get())) {
        sf::error(frame_processing_utils, "Failed to parse video info from caps");
        return;
    }

    // Validate output buffer size is at least the required size
    // Buffer can be larger due to alignment/padding, but must be at least the required size
    const gsize required_size = video_info.size;
    const gsize output_size = gst_buffer_get_size(output_buffer.get());
    if (output_size < required_size) {
        sf::error(frame_processing_utils,
                  "Output buffer size (%zu) is smaller than required video frame size (%zu). "
                  "Buffer should be created with create_output_buffer(input_buffer, props) to ensure correct size.",
                  output_size, required_size);
        return;
    }
    // Note: Buffer can be larger than required_size - that's fine, we'll only use the required amount

    // Convert our custom rectangles to rectpack2D format for the external library
    std::vector<sf::rectangle> input_rects_rectpack2d;
    std::vector<sf::rectangle> packed_rects_rectpack2d;

    input_rects_rectpack2d.reserve(layout.input_rectangles.size());
    for (const auto &rect: layout.input_rectangles) {
        input_rects_rectpack2d.emplace_back(rect.left, rect.top, rect.width, rect.height);
    }

    packed_rects_rectpack2d.reserve(layout.packed_rectangles.size());
    for (const auto &rect: layout.packed_rectangles) {
        packed_rects_rectpack2d.emplace_back(rect.left, rect.top, rect.width, rect.height);
    }

    // Use GstVideoFrame API for NV12 format (handles hardware buffers correctly)
    if (props.format && sf::string_to_video_format(props.format) == sf::video_format::nv12) {
        // Optimize NV12: consolidate fill and copy into single map/unmap cycle
        GstVideoInfo nv12_video_info;
        gst_video_info_init(&nv12_video_info);
        if (!gst_video_info_set_format(&nv12_video_info, GST_VIDEO_FORMAT_NV12, static_cast<guint>(props.width),
                                       static_cast<guint>(props.height))) {
            sf::error(frame_processing_utils, "Failed to set video info format for NV12");
            return;
        }

        // Map both buffers once for all operations (fill + copy)
        GstVideoFrame input_frame;
        GstVideoFrame output_frame;

        if (!gst_video_frame_map(&input_frame, &nv12_video_info, input_buffer.get(), GST_MAP_READ)) {
            sf::error(frame_processing_utils, "Failed to map input buffer for NV12 processing");
            return;
        }

        if (!gst_video_frame_map(&output_frame, &nv12_video_info, output_buffer.get(), GST_MAP_WRITE)) {
            gst_video_frame_unmap(&input_frame);
            sf::error(frame_processing_utils, "Failed to map output buffer for NV12 processing");
            return;
        }

        // Initialize output buffer with background color
        uint8_t luma_value_nv12 = 0x00;
        uint8_t uv_value_nv12 = 0x00;
        if (background_color == 0) {
            // Background color 0: Y=0x00, U=0x00, V=0x00
            luma_value_nv12 = 0x00;
            uv_value_nv12 = 0x00;
        } else {
            // For other colors, use default values (can be extended later)
            luma_value_nv12 = 0x00;
            uv_value_nv12 = 0x80;
        }

        auto *y_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, 0));
        const int luma_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&output_frame, 0);
        const int luma_height = GST_VIDEO_FRAME_COMP_HEIGHT(&output_frame, 0);
        for (int y = 0; y < luma_height; ++y) {
            std::memset(y_plane + y * luma_stride, luma_value_nv12, static_cast<size_t>(props.width));
        }

        auto *uv_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, 1));
        const int uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&output_frame, 1);
        const int uv_height = GST_VIDEO_FRAME_COMP_HEIGHT(&output_frame, 1);
        const int uv_width_bytes = props.width;
        for (int y = 0; y < uv_height; ++y) {
            for (int x = 0; x < uv_width_bytes; x += 2) {
                uv_plane[y * uv_stride + x] = uv_value_nv12;
                if (x + 1 < uv_width_bytes) {
                    uv_plane[y * uv_stride + x + 1] = uv_value_nv12;
                }
            }
        }

        // Copy regions using mapped frames (inline copy_regions_to_packed_frame_nv12 logic)
        const auto *y_in = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&input_frame, 0));
        auto luma_out = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, 0));
        const int luma_stride_in = GST_VIDEO_FRAME_PLANE_STRIDE(&input_frame, 0);
        const int luma_stride_out = GST_VIDEO_FRAME_PLANE_STRIDE(&output_frame, 0);
        auto uv_in = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&input_frame, 1));
        auto uv_out = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, 1));
        const int uv_stride_in = GST_VIDEO_FRAME_PLANE_STRIDE(&input_frame, 1);
        const int uv_stride_out = GST_VIDEO_FRAME_PLANE_STRIDE(&output_frame, 1);
        const int uv_plane_height = GST_VIDEO_FRAME_COMP_HEIGHT(&output_frame, 1);
        const int max_output_x = GST_VIDEO_FRAME_WIDTH(&output_frame);
        const int max_output_y = GST_VIDEO_FRAME_HEIGHT(&output_frame);

        for (size_t rect_index = 0; rect_index < packed_rects_rectpack2d.size(); ++rect_index) {
            if (rect_index >= input_rects_rectpack2d.size()) {
                break;
            }
            const auto &input_rect = input_rects_rectpack2d[rect_index];
            const auto &packed_rect = packed_rects_rectpack2d[rect_index];

            // Clamp rectangles to frame boundaries
            const int src_x = std::max(0, input_rect.left);
            const int src_y = std::max(0, input_rect.top);
            const int src_w = std::min(input_rect.width, props.width - src_x);
            const int src_h = std::min(input_rect.height, props.height - src_y);

            const int dst_x = std::max(0, packed_rect.left);
            const int dst_y = std::max(0, packed_rect.top);
            const int dst_w = std::min(packed_rect.width, max_output_x - dst_x);
            const int dst_h = std::min(packed_rect.height, max_output_y - dst_y);

            const int copy_w = std::min(src_w, dst_w);
            const int copy_h = std::min(src_h, dst_h);

            if (copy_w <= 0 || copy_h <= 0) {
                continue;
            }

            // Copy Y plane
            for (int y = 0; y < copy_h; ++y) {
                const int src_y_idx = (src_y + y) * luma_stride_in + src_x;
                const int dst_y_idx = (dst_y + y) * luma_stride_out + dst_x;
                std::memcpy(luma_out + dst_y_idx, y_in + src_y_idx, static_cast<size_t>(copy_w));
            }

            // Copy UV plane (interleaved U,V pairs) - with proper bounds validation
            const int uv_src_y = src_y / 2;
            const int uv_dst_y = dst_y / 2;
            const int uv_copy_h = (copy_h + 1) / 2;
            const int uv_copy_w_bytes = copy_w;

            // Validate bounds before copying to prevent buffer overruns
            const int dst_x_bytes = dst_x;
            const int src_x_bytes = src_x;

            // Validate source bounds
            if (src_x_bytes + uv_copy_w_bytes > uv_stride_in) {
                sf::error(frame_processing_utils,
                          "ROI %zu: Source UV copy out of bounds (src_x_bytes=%d + width_bytes=%d > stride_in=%d)",
                          rect_index, src_x_bytes, uv_copy_w_bytes, uv_stride_in);
                continue;
            }

            // Validate destination bounds - CRITICAL to prevent buffer overruns
            if (dst_x_bytes + uv_copy_w_bytes > uv_stride_out) {
                sf::error(
                        frame_processing_utils,
                        "ROI %zu: Destination UV copy out of bounds (dst_x_bytes=%d + width_bytes=%d > stride_out=%d)",
                        rect_index, dst_x_bytes, uv_copy_w_bytes, uv_stride_out);
                continue;
            }
            if (uv_dst_y + uv_copy_h > uv_plane_height) {
                sf::error(frame_processing_utils,
                          "ROI %zu: Destination UV copy out of bounds (dst_y=%d + height=%d > plane_height=%d)",
                          rect_index, uv_dst_y, uv_copy_h, uv_plane_height);
                continue;
            }

            // Copy UV plane rows
            for (int y = 0; y < uv_copy_h; ++y) {
                const int src_uv_idx = (uv_src_y + y) * uv_stride_in + src_x_bytes;
                const int dst_uv_idx = (uv_dst_y + y) * uv_stride_out + dst_x_bytes;
                std::memcpy(uv_out + dst_uv_idx, uv_in + src_uv_idx, static_cast<size_t>(uv_copy_w_bytes));
            }
        }

        // Unmap both buffers after all operations (single map/unmap cycle)
        gst_video_frame_unmap(&output_frame);
        gst_video_frame_unmap(&input_frame);
    } else {
        // Use GstMapInfo API for other formats (I420, etc.)
        GstMapInfo in_map_info;
        if (!gst_buffer_map(input_buffer.get(), &in_map_info, GST_MAP_READ)) {
            sf::error(frame_processing_utils, "Failed to map input buffer for reading.");
            return;
        }
        const sf::unique_gst_buffer_unmapper in_unmapper(input_buffer.get(), &in_map_info);

        GstMapInfo out_map_info;
        if (!gst_buffer_map(output_buffer.get(), &out_map_info, GST_MAP_WRITE)) {
            sf::error(frame_processing_utils, "Failed to map output buffer for writing.");
            return;
        }
        const sf::unique_gst_buffer_unmapper out_unmapper(output_buffer.get(), &out_map_info);

        const sf::video_format format_enum =
                props.format ? sf::string_to_video_format(props.format) : sf::video_format::i420;
        sf::copy_regions_to_packed_frame(in_map_info, out_map_info, props.width, props.height, input_rects_rectpack2d,
                                         packed_rects_rectpack2d, format_enum);
    }
}

void create_metadata_and_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                       sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                       const std::vector<sf::rectangle> &input_rectangles,
                                       const std::vector<sf::rectangle> &packed_rectangles,
                                       const uint32_t background_color) {
    const pipeline_app_adapter app_adapter(data);
    const auto frame_contract = get_frame_contract_from_buffer(input_buffer.get());
    if (!frame_contract.has_value()) {
        data.metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop(
                "missing frame contract metadata while creating ROI metadata bundle");
        sf::error(frame_processing_utils, "Cannot create metadata bundle without frame contract metadata");
        return;
    }

    const creation_params params = {.reconstructed_width = props.width,
                                    .reconstructed_height = props.height,
                                    .log2_unit_size = 0,
                                    .persistence_flag = false};

    // Convert rectangles for create_packed_regions_info
    std::vector<sf::rectangle> input_rectangles_for_metadata;
    std::vector<sf::rectangle> packed_rectangles_for_metadata;

    input_rectangles_for_metadata.reserve(input_rectangles.size());
    packed_rectangles_for_metadata.reserve(packed_rectangles.size());

    for (const auto &rect: input_rectangles) {
        input_rectangles_for_metadata.emplace_back(rect.left, rect.top, rect.width, rect.height);
    }
    for (const auto &rect: packed_rectangles) {
        packed_rectangles_for_metadata.emplace_back(rect.left, rect.top, rect.width, rect.height);
    }

    const auto packed_regions_data =
            create_packed_regions_info(input_rectangles_for_metadata, packed_rectangles_for_metadata, params);
    data.packed_regions_queue.push({.sequence_id = frame_contract->sequence_id, .regions = packed_regions_data});
    data.metadata_runtime.record_bundle_enqueued();

    const guint64 pts = GST_BUFFER_PTS(input_buffer.get());
    const guint64 dts = GST_BUFFER_DTS(input_buffer.get());
    int trace_frame_number = -1;
    if (GST_BUFFER_OFFSET_IS_VALID(input_buffer.get())) {
        trace_frame_number = static_cast<int>(GST_BUFFER_OFFSET(input_buffer.get()));
    }
    dump_pri_regions_trace(make_packing_config(data), trace_frame_number, static_cast<uint64_t>(pts),
                           GST_BUFFER_PTS_IS_VALID(input_buffer.get()), static_cast<uint64_t>(dts),
                           GST_BUFFER_DTS_IS_VALID(input_buffer.get()), input_rectangles_for_metadata,
                           packed_rectangles_for_metadata);

    if (data.params.debug.draw_borders) {
        data.display_regions_queue.push({.sequence_id = frame_contract->sequence_id, .regions = packed_regions_data});
        data.metadata_runtime.record_bundle_enqueued();
    }
    if (data.params.debug.save_frames) {
        data.debug_regions_queue.push({.sequence_id = frame_contract->sequence_id, .regions = packed_regions_data});
        data.metadata_runtime.record_bundle_enqueued();
    }

    // Copy pixel data using the packed positions for visual output
    frame_layout layout;
    layout.input_rectangles = input_rectangles_for_metadata;
    layout.packed_rectangles = packed_rectangles_for_metadata;

    // Validate buffers before copying
    if (!input_buffer || !output_buffer) {
        sf::error(frame_processing_utils, "Invalid buffer pointers in create_metadata_and_process_frame");
        return;
    }

    copy_and_pack_pixel_data(input_buffer, output_buffer, props, layout, background_color);

    sf::debug(frame_processing_utils, "Visual output: Using packed positions for %zu ROIs", packed_rectangles.size());
}
