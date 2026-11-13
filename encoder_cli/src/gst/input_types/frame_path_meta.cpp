// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "frame_path_meta.h"

#include "../../../../libvcai/include/logging.h"

#include <gst/gst.h>

/**
 * @file frame_path_meta.cpp
 * @brief Implementation of frame path metadata for dual-path bypass mode.
 */

/// GStreamer debug category for frame path metadata.
GST_DEBUG_CATEGORY_STATIC(frame_path_meta_cat);

namespace {

    /**
     * @brief Initializes the debug category for frame path metadata once.
     */
    void init_debug_category() {
        static bool initialized = false;
        if (!initialized) {
            GST_DEBUG_CATEGORY_INIT(frame_path_meta_cat, "frame-path-meta", 0, "Frame path metadata");
            initialized = true;
        }
    }

#define FRAME_PATH_META_API_TYPE (frame_path_meta_api_get_type())
#define FRAME_PATH_META_INFO (frame_path_meta_get_info())

    /**
     * @brief Registers and returns the GType for the frame path meta API.
     */
    GType frame_path_meta_api_get_type_impl() {
        static GType type = 0;
        static const gchar *tags[] = {nullptr};

        if (g_once_init_enter(&type)) {
            const GType api_type = gst_meta_api_type_register("FramePathMetaAPI", tags);
            g_once_init_leave(&type, api_type);
        }
        return type;
    }

    /**
     * @brief Initializes a frame_path_meta instance for a new buffer.
     * @param meta Meta structure to initialize.
     * @param params Unused meta initialization parameters.
     * @param buffer Buffer receiving the meta.
     * @return TRUE on success.
     */
    gboolean frame_path_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
        auto *frame_meta = reinterpret_cast<frame_path_meta *>(meta);
        frame_meta->path = frame_path_type::inference; // Default
        frame_meta->sequence_id = 0;
        (void) params;
        (void) buffer;
        return TRUE;
    }

    /**
     * @brief Frees a frame_path_meta instance (no-op for POD data).
     * @param meta Meta structure to release.
     * @param buffer Buffer owning the meta.
     */
    void frame_path_meta_free(GstMeta *meta, GstBuffer *buffer) {
        (void) meta;
        (void) buffer;
    }

    /**
     * @brief Copies frame path metadata between buffers.
     * @param dest Destination buffer.
     * @param meta Source meta data.
     * @param source Source buffer.
     * @param type Transform type.
     * @param data Additional transform data.
     * @return TRUE if metadata was copied successfully.
     */
    gboolean frame_path_meta_transform(GstBuffer *dest, GstMeta *meta, GstBuffer *source, GQuark type, gpointer data) {
        auto *frame_meta = reinterpret_cast<frame_path_meta *>(meta);
        frame_path_meta *dest_meta =
                reinterpret_cast<frame_path_meta *>(gst_buffer_add_meta(dest, FRAME_PATH_META_INFO, nullptr));
        if (!dest_meta) {
            return FALSE;
        }
        dest_meta->path = frame_meta->path;
        (void) source;
        (void) type;
        (void) data;
        return TRUE;
    }

    /**
     * @brief Registers and returns the GstMetaInfo for frame path metadata.
     */
    const GstMetaInfo *frame_path_meta_get_info_impl() {
        static const GstMetaInfo *meta_info = nullptr;

        if (g_once_init_enter(&meta_info)) {
            const GstMetaInfo *mi =
                    gst_meta_register(FRAME_PATH_META_API_TYPE, "FramePathMeta", sizeof(frame_path_meta),
                                      frame_path_meta_init, frame_path_meta_free, frame_path_meta_transform);
            g_once_init_leave(&meta_info, mi);
        }
        return meta_info;
    }

} // namespace

GType frame_path_meta_api_get_type() {
    init_debug_category();
    return frame_path_meta_api_get_type_impl();
}

const GstMetaInfo *frame_path_meta_get_info() {
    init_debug_category();
    return frame_path_meta_get_info_impl();
}

bool attach_frame_path_metadata(sf::unique_gst_sample_ptr &sample_ptr, const frame_path_type path,
                                const uint64_t sequence_id) {
    init_debug_category();
    if (!sample_ptr) {
        return false;
    }

    GstSample *sample = sample_ptr.get();
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        sf::error(frame_path_meta_cat, "Failed to get buffer from sample");
        return false;
    }

    if (!gst_buffer_is_writable(buffer)) {
        GstBuffer *copied_buffer = gst_buffer_copy_deep(buffer);
        if (copied_buffer == nullptr) {
            sf::error(frame_path_meta_cat, "Failed to deep-copy sample buffer for contract metadata attach");
            return false;
        }

        sf::unique_gst_sample_ptr replacement_sample(
                gst_sample_new(copied_buffer, gst_sample_get_caps(sample), gst_sample_get_segment(sample), nullptr));
        gst_buffer_unref(copied_buffer);
        if (!replacement_sample) {
            sf::error(frame_path_meta_cat, "Failed to rebuild sample after deep-copy conversion");
            return false;
        }

        sample_ptr = std::move(replacement_sample);
        sample = sample_ptr.get();
        buffer = gst_sample_get_buffer(sample);
        if (!buffer) {
            sf::error(frame_path_meta_cat, "Replacement sample has no buffer");
            return false;
        }
    }

    return attach_frame_contract_metadata_to_buffer(buffer, path, sequence_id);
}

bool attach_frame_contract_metadata_to_buffer(GstBuffer *buffer, const frame_path_type path,
                                              const uint64_t sequence_id) {
    init_debug_category();
    if (buffer == nullptr) {
        return false;
    }
    if (!gst_buffer_is_writable(buffer)) {
        sf::error(frame_path_meta_cat, "Cannot attach frame contract metadata to non-writable buffer");
        return false;
    }

    frame_path_meta *meta =
            reinterpret_cast<frame_path_meta *>(gst_buffer_add_meta(buffer, FRAME_PATH_META_INFO, nullptr));
    if (!meta) {
        sf::error(frame_path_meta_cat, "Failed to add frame contract metadata to buffer");
        return false;
    }

    meta->path = path;
    meta->sequence_id = sequence_id;
    return true;
}

std::optional<frame_contract_info> get_frame_contract_from_buffer(const GstBuffer *buffer) {
    init_debug_category();
    if (buffer == nullptr) {
        return std::nullopt;
    }

    const frame_path_meta *meta = reinterpret_cast<const frame_path_meta *>(
            gst_buffer_get_meta(const_cast<GstBuffer *>(buffer), FRAME_PATH_META_API_TYPE));
    if (meta == nullptr || meta->sequence_id == 0) {
        return std::nullopt;
    }

    return frame_contract_info{.path = meta->path, .sequence_id = meta->sequence_id};
}

frame_path_type get_frame_path_from_buffer(const GstBuffer *buffer) {
    const auto contract_info = get_frame_contract_from_buffer(buffer);
    return contract_info.has_value() ? contract_info->path : frame_path_type::inference;
}

bool is_bypass_frame(const GstBuffer *buffer) { return get_frame_path_from_buffer(buffer) == frame_path_type::bypass; }
