// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "meta/sf_sei_meta.h"

static gboolean sf_sei_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    sf_sei_meta *sei_meta = reinterpret_cast<sf_sei_meta *>(meta);
    (void) params;
    (void) buffer;

    sei_meta->pri_data = nullptr;
    sei_meta->pri_size = 0;
    sei_meta->eoi_data = nullptr;
    sei_meta->eoi_size = 0;
    return TRUE;
}

static void sf_sei_meta_free(GstMeta *meta, GstBuffer *buffer) {
    sf_sei_meta *sei_meta = reinterpret_cast<sf_sei_meta *>(meta);
    (void) buffer;

    g_free(sei_meta->pri_data);
    g_free(sei_meta->eoi_data);
}

static gboolean sf_sei_meta_transform(GstBuffer *dest, GstMeta *meta, GstBuffer *src, GQuark type, gpointer data) {
    sf_sei_meta *src_meta = reinterpret_cast<sf_sei_meta *>(meta);
    (void) src;
    (void) data;

    if (!GST_META_TRANSFORM_IS_COPY(type)) {
        return FALSE;
    }

    return sf_buffer_add_sei_meta(dest, src_meta->pri_data, src_meta->pri_size, src_meta->eoi_data,
                                  src_meta->eoi_size) != nullptr;
}

GType sf_sei_meta_api_get_type(void) {
    static gsize type_storage = 0;
    static const gchar *tags[] = {"video", "sei", "vcai", nullptr};

    if (g_once_init_enter(&type_storage)) {
        GType registered_type = gst_meta_api_type_register("sf_sei_metaAPI", tags);
        g_once_init_leave(&type_storage, static_cast<gsize>(registered_type));
    }

    return static_cast<GType>(type_storage);
}

const GstMetaInfo *sf_sei_meta_get_info(void) {
    static gsize info_once = 0;
    static const GstMetaInfo *info = nullptr;

    if (g_once_init_enter(&info_once)) {
        info = gst_meta_register(SF_SEI_META_API_TYPE, "sf_sei_meta", sizeof(sf_sei_meta), sf_sei_meta_init,
                                 sf_sei_meta_free, sf_sei_meta_transform);
        g_once_init_leave(&info_once, 1);
    }

    return info;
}

/** @brief Replaces *dst_data with a copy of src_data; frees old *dst_data. @return TRUE on success. */
static gboolean sf_replace_blob(guint8 **dst_data, gsize *dst_size, const guint8 *src_data, gsize src_size) {
    guint8 *copied_data = nullptr;

    if (src_data != nullptr && src_size > 0) {
        copied_data = static_cast<guint8 *>(g_memdup2(src_data, src_size));
        if (copied_data == nullptr) {
            return FALSE;
        }
    }

    g_free(*dst_data);
    *dst_data = copied_data;
    *dst_size = src_size;
    return TRUE;
}

sf_sei_meta *sf_buffer_add_sei_meta(GstBuffer *buffer, const guint8 *pri_data, gsize pri_size, const guint8 *eoi_data,
                                    gsize eoi_size) {
    sf_sei_meta *meta = nullptr;

    g_return_val_if_fail(GST_IS_BUFFER(buffer), nullptr);
    g_return_val_if_fail((pri_data != nullptr || pri_size == 0), nullptr);
    g_return_val_if_fail((eoi_data != nullptr || eoi_size == 0), nullptr);

    meta = sf_buffer_get_sei_meta(buffer);
    if (meta == nullptr) {
        meta = reinterpret_cast<sf_sei_meta *>(gst_buffer_add_meta(buffer, SF_SEI_META_INFO, nullptr));
        if (meta == nullptr) {
            return nullptr;
        }
    }

    if (!sf_replace_blob(&meta->pri_data, &meta->pri_size, pri_data, pri_size)) {
        return nullptr;
    }

    if (!sf_replace_blob(&meta->eoi_data, &meta->eoi_size, eoi_data, eoi_size)) {
        return nullptr;
    }

    return meta;
}
