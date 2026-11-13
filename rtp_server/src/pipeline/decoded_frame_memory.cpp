// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "decoded_frame_memory.h"

#include <cstring>
#include <gst/video/video-info.h>
#include <string>
#include <string_view>

#include "context/pipeline_contexts.h"
#include "logging.h"
#include "raw_video_caps_metadata.h"

// Debug category for pipeline-level logging (defined in pipeline.cpp).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

namespace {
    [[nodiscard]] const char *cpu_mutation_stage_to_string(const cpu_mutation_stage stage) {
        switch (stage) {
            case cpu_mutation_stage::pri_reconstruction:
                return "pri_reconstruction";
            case cpu_mutation_stage::luma_backscale:
                return "luma_backscale";
            case cpu_mutation_stage::tet_apply:
                return "tet_apply";
            default:
                return "unknown";
        }
    }

    void record_gpu_to_cpu_conversion(pipeline_statistics *const stats) {
        if (stats) {
            ++stats->gpu_to_cpu_sample_conversions;
        }
    }

    void record_writable_buffer_conversion(pipeline_statistics *const stats) {
        if (stats) {
            ++stats->writable_buffer_conversions;
        }
    }

    void record_cpu_mutation_stage_failure(pipeline_statistics *const stats) {
        if (stats) {
            ++stats->cpu_mutation_stage_failures;
        }
    }

    [[nodiscard]] bool caps_features_indicate_gpu_memory(const GstCaps *caps) {
        if (!caps || gst_caps_get_size(caps) == 0) {
            return false;
        }

        const GstCapsFeatures *features = gst_caps_get_features(caps, 0);
        if (!features) {
            return false;
        }

        return gst_caps_features_contains(features, "memory:NVMM") ||
               gst_caps_features_contains(features, "memory:CUDAMemory") ||
               gst_caps_features_contains(features, "memory:CUDA");
    }

    [[nodiscard]] bool structure_fields_indicate_gpu_memory(const GstStructure *structure) {
        if (!structure) {
            return false;
        }

        if (const gchar *memory_type = gst_structure_get_string(structure, "memory"); memory_type != nullptr) {
            const std::string_view memory_view(memory_type);
            if (memory_view == "CUDAMemory" || memory_view == "CUDA" || memory_view == "NVMM") {
                return true;
            }
        }

        if (const gchar *nvbuf_memory_type = gst_structure_get_string(structure, "nvbuf-memory-type");
            nvbuf_memory_type != nullptr && nvbuf_memory_type[0] != '\0') {
            return true;
        }

        return false;
    }
} // namespace

[[nodiscard]] decoded_memory_domain classify_memory_domain_from_caps(const GstCaps *caps) {
    return is_cuda_memory_caps(caps) ? decoded_memory_domain::gpu_backed : decoded_memory_domain::system;
}

/**
 * @brief Checks if caps describe CUDA memory buffers.
 * @param caps Caps to inspect.
 * @return true when caps indicate CUDA memory.
 */
[[nodiscard]] bool is_cuda_memory_caps(const GstCaps *caps) {
    if (!caps) {
        return false;
    }

    if (caps_features_indicate_gpu_memory(caps)) {
        return true;
    }

    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    return structure_fields_indicate_gpu_memory(structure);
}

/**
 * @brief Copies a GstBuffer from CUDA memory to system memory.
 * @param cuda_buffer The buffer in CUDA memory.
 * @param caps The caps describing the buffer format.
 * @return A new buffer in system memory, or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr
copy_cuda_buffer_to_system_memory(const sf::unique_gst_buffer_no_delete_ptr &cuda_buffer,
                                  const sf::unique_gst_caps_no_delete_ptr &caps) {
    if (!cuda_buffer || !caps) {
        sf::error(pipeline_category, "Invalid parameters for buffer copy");
        return nullptr;
    }

    // Parse video info from caps
    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, caps.get())) {
        sf::error(pipeline_category, "Failed to parse video info from caps");
        return nullptr;
    }

    // Map CUDA buffer for reading
    GstMapInfo cuda_map_info;
    if (!gst_buffer_map(cuda_buffer.get(), &cuda_map_info, GST_MAP_READ)) {
        sf::error(pipeline_category, "Failed to map CUDA buffer for reading");
        return nullptr;
    }

    // Allocate new system memory buffer with proper size
    auto system_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, video_info.size, nullptr));
    if (!system_buffer) {
        sf::error(pipeline_category, "Failed to allocate system memory buffer");
        gst_buffer_unmap(cuda_buffer.get(), &cuda_map_info);
        return nullptr;
    }

    // Map system buffer for writing
    GstMapInfo system_map_info;
    if (!gst_buffer_map(system_buffer.get(), &system_map_info, GST_MAP_WRITE)) {
        sf::error(pipeline_category, "Failed to map system buffer for writing");
        gst_buffer_unmap(cuda_buffer.get(), &cuda_map_info);
        return nullptr;
    }

    // Copy plane by plane to handle strides correctly
    // For YUV420 (I420), we have 3 planes: Y, U, V
    const guint num_planes = GST_VIDEO_INFO_N_PLANES(&video_info);
    for (guint plane = 0; plane < num_planes; ++plane) {
        const gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&video_info, plane);
        const gint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE(&video_info, plane);

        // Get plane pointers
        const guint8 *src_plane = cuda_map_info.data + GST_VIDEO_INFO_PLANE_OFFSET(&video_info, plane);
        guint8 *dst_plane = system_map_info.data + GST_VIDEO_INFO_PLANE_OFFSET(&video_info, plane);

        // Copy each row respecting the stride
        // For planar formats like I420, pixel stride is typically 1 byte per component
        const gint comp_width = GST_VIDEO_INFO_COMP_WIDTH(&video_info, plane);
        const gint pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&video_info, plane);
        const gsize row_size = static_cast<gsize>(comp_width) * static_cast<gsize>(pixel_stride);
        for (gint row = 0; row < plane_height; ++row) {
            std::memcpy(dst_plane + static_cast<gsize>(row) * static_cast<gsize>(plane_stride),
                        src_plane + static_cast<gsize>(row) * static_cast<gsize>(plane_stride), row_size);
        }
    }

    // Unmap buffers
    gst_buffer_unmap(system_buffer.get(), &system_map_info);
    gst_buffer_unmap(cuda_buffer.get(), &cuda_map_info);

    // Copy timing information
    GST_BUFFER_PTS(system_buffer.get()) = GST_BUFFER_PTS(cuda_buffer.get());
    GST_BUFFER_DTS(system_buffer.get()) = GST_BUFFER_DTS(cuda_buffer.get());
    GST_BUFFER_DURATION(system_buffer.get()) = GST_BUFFER_DURATION(cuda_buffer.get());
    GST_BUFFER_OFFSET(system_buffer.get()) = GST_BUFFER_OFFSET(cuda_buffer.get());
    GST_BUFFER_OFFSET_END(system_buffer.get()) = GST_BUFFER_OFFSET_END(cuda_buffer.get());

    return system_buffer;
}

/**
 * @brief Converts a CUDA memory sample to a system memory sample.
 * @param cuda_sample The sample in CUDA memory.
 * @param frame_number Frame number for logging.
 * @param port Port number for logging.
 * @return System memory sample or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_sample_ptr
convert_cuda_sample_to_system_memory(const sf::unique_gst_sample_ptr &cuda_sample, const guint64 frame_number,
                                     const guint16 port) {
    auto *const input_buffer = gst_sample_get_buffer(cuda_sample.get());
    const GstCaps *input_caps = gst_sample_get_caps(cuda_sample.get());

    // Wrap non-owning pointers in no-delete wrappers
    sf::unique_gst_buffer_no_delete_ptr cuda_buffer_wrapper(input_buffer);
    sf::unique_gst_caps_no_delete_ptr caps_wrapper(const_cast<GstCaps *>(input_caps));

    auto system_buffer = copy_cuda_buffer_to_system_memory(cuda_buffer_wrapper, caps_wrapper);
    if (!system_buffer) {
        sf::error(pipeline_category, "Failed to copy CUDA buffer to system memory for frame #%lu on port %u",
                  frame_number, port);
        return nullptr;
    }

    // Create new sample with system memory buffer
    const auto *const segment = gst_sample_get_segment(cuda_sample.get());
    const auto *const info = gst_sample_get_info(cuda_sample.get());
    sf::unique_gst_structure_ptr info_copy(info ? gst_structure_copy(info) : nullptr);

    // Rebuild caps for the system-memory sample while keeping negotiated
    // raw-video metadata that affects downstream interpretation.
    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, input_caps)) {
        sf::error(pipeline_category, "Failed to parse video info from caps for frame #%lu on port %u", frame_number,
                  port);
        info_copy.reset();
        return nullptr;
    }

    auto system_caps = sf::unique_gst_caps_ptr(gst_video_info_to_caps(&video_info));
    if (!system_caps) {
        sf::error(pipeline_category, "Failed to create system caps for frame #%lu on port %u", frame_number, port);
        info_copy.reset();
        return nullptr;
    }

    const GstStructure *input_structure = gst_caps_get_structure(input_caps, 0);
    GstStructure *caps_structure = gst_caps_get_structure(system_caps.get(), 0);
    if (caps_structure) {
        copy_preserved_raw_video_caps_fields(input_structure, caps_structure);
        gst_structure_remove_field(caps_structure, "memory");
        gst_structure_remove_field(caps_structure, "nvbuf-memory-type");
        gst_structure_remove_field(caps_structure, "block-linear");
        gst_structure_remove_field(caps_structure, "gpu-id");
    }

    // gst_sample_new adds its own reference to caps, so we release our ownership
    auto sample = sf::unique_gst_sample_ptr(
            gst_sample_new(system_buffer.get(), system_caps.get(), segment, info_copy.release()));

    // Release our references so sample owns the data
    system_buffer.reset();
    system_caps.reset();
    return sample;
}

[[nodiscard]] sf::unique_gst_sample_ptr ensure_system_memory_sample_for_cpu_mutation(sf::unique_gst_sample_ptr sample,
                                                                                     const guint64 frame_number,
                                                                                     const guint16 port,
                                                                                     const cpu_mutation_stage stage,
                                                                                     pipeline_statistics *const stats) {
    if (!sample) {
        sf::error(pipeline_category, "CPU mutation stage %s received null sample for frame #%lu on port %u",
                  cpu_mutation_stage_to_string(stage), frame_number, port);
        record_cpu_mutation_stage_failure(stats);
        return nullptr;
    }

    const GstCaps *input_caps = gst_sample_get_caps(sample.get());
    if (classify_memory_domain_from_caps(input_caps) == decoded_memory_domain::gpu_backed) {
        sf::debug(pipeline_category,
                  "CPU mutation stage %s requires system memory; converting GPU-backed sample for frame #%lu on "
                  "port %u",
                  cpu_mutation_stage_to_string(stage), frame_number, port);
        auto converted_sample = convert_cuda_sample_to_system_memory(sample, frame_number, port);
        if (!converted_sample) {
            sf::error(pipeline_category,
                      "Failed to convert sample to system memory for CPU mutation stage %s, frame #%lu on port %u",
                      cpu_mutation_stage_to_string(stage), frame_number, port);
            record_cpu_mutation_stage_failure(stats);
            return nullptr;
        }
        record_gpu_to_cpu_conversion(stats);
        return converted_sample;
    }

    return sample;
}

[[nodiscard]] sf::unique_gst_sample_ptr ensure_writable_sample_for_cpu_mutation(sf::unique_gst_sample_ptr sample,
                                                                                const guint64 frame_number,
                                                                                const guint16 port,
                                                                                const cpu_mutation_stage stage,
                                                                                pipeline_statistics *const stats) {
    if (!sample) {
        sf::error(pipeline_category,
                  "CPU mutation stage %s cannot ensure writability for null sample (frame #%lu on port %u)",
                  cpu_mutation_stage_to_string(stage), frame_number, port);
        record_cpu_mutation_stage_failure(stats);
        return nullptr;
    }

    auto *const buffer = gst_sample_get_buffer(sample.get());
    if (!buffer) {
        sf::error(pipeline_category, "CPU mutation stage %s sample has no buffer for frame #%lu on port %u",
                  cpu_mutation_stage_to_string(stage), frame_number, port);
        record_cpu_mutation_stage_failure(stats);
        return nullptr;
    }

    if (gst_buffer_is_writable(buffer)) {
        return sample;
    }

    sf::debug(pipeline_category, "CPU mutation stage %s creating writable buffer copy for frame #%lu on port %u",
              cpu_mutation_stage_to_string(stage), frame_number, port);
    sf::unique_gst_buffer_ptr writable_buffer(gst_buffer_make_writable(gst_buffer_ref(buffer)));
    if (!writable_buffer) {
        sf::error(pipeline_category,
                  "Failed to create writable buffer for CPU mutation stage %s, frame #%lu on port %u",
                  cpu_mutation_stage_to_string(stage), frame_number, port);
        record_cpu_mutation_stage_failure(stats);
        return nullptr;
    }

    const GstCaps *caps = gst_sample_get_caps(sample.get());
    const GstSegment *segment = gst_sample_get_segment(sample.get());
    const GstStructure *info = gst_sample_get_info(sample.get());
    sf::unique_gst_structure_ptr info_copy(info ? gst_structure_copy(info) : nullptr);

    auto writable_sample = sf::unique_gst_sample_ptr(
            gst_sample_new(writable_buffer.get(), const_cast<GstCaps *>(caps), segment, info_copy.release()));
    if (!writable_sample) {
        sf::error(pipeline_category, "Failed to build writable sample for CPU mutation stage %s, frame #%lu on port %u",
                  cpu_mutation_stage_to_string(stage), frame_number, port);
        record_cpu_mutation_stage_failure(stats);
        return nullptr;
    }

    record_writable_buffer_conversion(stats);
    writable_buffer.reset();
    return writable_sample;
}

[[nodiscard]] sf::unique_gst_sample_ptr ensure_system_memory_sample(sf::unique_gst_sample_ptr sample,
                                                                    const guint64 frame_number, const guint16 port) {
    return ensure_system_memory_sample_for_cpu_mutation(std::move(sample), frame_number, port,
                                                        cpu_mutation_stage::pri_reconstruction, nullptr);
}

[[nodiscard]] sf::unique_gst_buffer_ptr
prepare_buffer_for_push_with_domain_check(const sf::unique_gst_sample_ptr &sample, const guint64 frame_number,
                                          const guint16 port) {
    if (!sample) {
        sf::error(pipeline_category, "Null sample while preparing push buffer for frame #%lu on port %u", frame_number,
                  port);
        return nullptr;
    }

    auto *const buffer = gst_sample_get_buffer(sample.get());
    if (!buffer) {
        sf::error(pipeline_category, "Sample has no buffer for frame #%lu on port %u", frame_number, port);
        return nullptr;
    }

    const GstCaps *original_caps = gst_sample_get_caps(sample.get());
    if (classify_memory_domain_from_caps(original_caps) == decoded_memory_domain::gpu_backed) {
        sf::error(pipeline_category,
                  "Decoded appsrc contract violation: GPU-backed buffer reached CPU-mutation push boundary for "
                  "frame #%lu on port %u",
                  frame_number, port);
        return nullptr;
    }

    return sf::unique_gst_buffer_ptr(gst_buffer_ref(buffer));
}

[[nodiscard]] sf::unique_gst_buffer_ptr prepare_buffer_for_push(const sf::unique_gst_sample_ptr &sample,
                                                                const guint64 frame_number, const guint16 port) {
    return prepare_buffer_for_push_with_domain_check(sample, frame_number, port);
}
