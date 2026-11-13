// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "elements/sf_h265_sei_extract.h"
#include "core/sf_frame_identity.h"
#include "core/sf_sei_sync_store.h"
#include "meta/sf_sei_meta.h"

#include <gst/gst.h>
#include <h265_sps_parser.h>
#include <optional>

GST_DEBUG_CATEGORY_STATIC(sf_h265_sei_extract_category);
#define GST_CAT_DEFAULT sf_h265_sei_extract_category

enum {
    k_h265_nal_prefix_sei = 39,
    k_h265_nal_suffix_sei = 40,
    k_h265_nal_sps = 34,
    k_h265_sei_payload_type_eoi = 215,
    k_h265_sei_payload_type_pri = 226
};

struct _sf_h265_sei_extract {
    GstBaseTransform parent;
    guint64 produced_entries;
    guint64 publish_failures;
};

G_DEFINE_TYPE(sf_h265_sei_extract, sf_h265_sei_extract, GST_TYPE_BASE_TRANSFORM)

static GstStaticPadTemplate sink_template =
        GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-h265"));

static GstStaticPadTemplate src_template =
        GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-h265"));

typedef struct {
    guint8 *data;
    gsize size;
} sf_payload_blob;

/** @brief Finds next Annex B start code (3- or 4-byte) from offset. Returns byte index or -1. */
static gint sf_find_start_code(const guint8 *data, gsize size, gsize offset, gsize *prefix_size) {
    gsize index = offset;
    while (index + 3 < size) {
        if (data[index] == 0x00 && data[index + 1] == 0x00 && data[index + 2] == 0x01) {
            *prefix_size = 3;
            return static_cast<gint>(index);
        }
        if (index + 4 < size && data[index] == 0x00 && data[index + 1] == 0x00 && data[index + 2] == 0x00 &&
            data[index + 3] == 0x01) {
            *prefix_size = 4;
            return static_cast<gint>(index);
        }
        ++index;
    }

    return -1;
}

/** @brief Frees blob data and zeroes size. */
static void sf_payload_blob_reset(sf_payload_blob *blob) {
    g_free(blob->data);
    blob->data = nullptr;
    blob->size = 0;
}

/** @brief Replaces blob contents with a copy of data; frees previous. @return TRUE on success. */
static gboolean sf_payload_blob_replace(sf_payload_blob *blob, const guint8 *data, gsize size) {
    guint8 *copy = nullptr;

    if (size > 0) {
        copy = static_cast<guint8 *>(g_memdup2(data, size));
        if (copy == nullptr) {
            return FALSE;
        }
    }

    g_free(blob->data);
    blob->data = copy;
    blob->size = size;
    return TRUE;
}

/** @brief Strips H.265 emulation-prevention bytes (0x03 after 0x00 0x00). Caller frees result. */
static guint8 *sf_rbsp_from_ebsp(const guint8 *data, gsize size, gsize *rbsp_size) {
    gsize in_index = 0;
    gsize out_index = 0;
    guint8 zero_run = 0;
    guint8 *rbsp = static_cast<guint8 *>(g_malloc(size));

    if (rbsp == nullptr) {
        return nullptr;
    }

    while (in_index < size) {
        const guint8 byte = data[in_index++];
        if (zero_run == 2 && byte == 0x03) {
            zero_run = 0;
            continue;
        }

        rbsp[out_index++] = byte;
        if (byte == 0x00) {
            ++zero_run;
        } else {
            zero_run = 0;
        }
    }

    *rbsp_size = out_index;
    return rbsp;
}

/** @brief Parses SEI payload type/size and fills pri_payload/eoi_payload for PRI (226) and EOI (215). */
static void sf_parse_sei_rbsp_messages(const guint8 *rbsp, gsize rbsp_size, sf_payload_blob *pri_payload,
                                       sf_payload_blob *eoi_payload) {
    gsize offset = 0;

    while (offset + 1 < rbsp_size) {
        guint payload_type = 0;
        gsize payload_size = 0;

        while (offset < rbsp_size && rbsp[offset] == 0xFF) {
            payload_type += 255;
            ++offset;
        }
        if (offset >= rbsp_size) {
            break;
        }
        payload_type += rbsp[offset++];

        while (offset < rbsp_size && rbsp[offset] == 0xFF) {
            payload_size += 255;
            ++offset;
        }
        if (offset >= rbsp_size) {
            break;
        }
        payload_size += rbsp[offset++];

        if (payload_size > (rbsp_size - offset)) {
            GST_WARNING("Invalid SEI payload size encountered (%" G_GSIZE_FORMAT " > %" G_GSIZE_FORMAT ")",
                        payload_size, rbsp_size - offset);
            break;
        }

        if (payload_type == k_h265_sei_payload_type_pri) {
            (void) sf_payload_blob_replace(pri_payload, rbsp + offset, payload_size);
        } else if (payload_type == k_h265_sei_payload_type_eoi) {
            (void) sf_payload_blob_replace(eoi_payload, rbsp + offset, payload_size);
        }

        offset += payload_size;

        if (offset < rbsp_size && rbsp[offset] == 0x80) {
            gboolean only_trailing_padding = TRUE;
            for (gsize trailing_index = offset + 1; trailing_index < rbsp_size; ++trailing_index) {
                if (rbsp[trailing_index] != 0x00) {
                    only_trailing_padding = FALSE;
                    break;
                }
            }
            if (only_trailing_padding) {
                break;
            }
        }
    }
}

/** @brief Scans Annex B NALs, extracts RBSP from prefix SEI NALs, parses and fills PRI/EOI blobs.
 *  When \a stream_id is non-null, SPS NALs are parsed and base QP is stored for dynamic TET. */
static void sf_extract_pri_eoi_from_annexb(const guint8 *data, gsize size, sf_payload_blob *pri_payload,
                                           sf_payload_blob *eoi_payload, const gchar *stream_id) {
    gsize search_offset = 0;

    while (search_offset < size) {
        gsize prefix_size = 0;
        const gint start_code_index = sf_find_start_code(data, size, search_offset, &prefix_size);
        if (start_code_index < 0) {
            break;
        }

        const gsize nal_start = static_cast<gsize>(start_code_index) + prefix_size;
        gsize next_prefix_size = 0;
        const gint next_start_code_index = sf_find_start_code(data, size, nal_start + 2, &next_prefix_size);
        const gsize nal_end = (next_start_code_index < 0) ? size : static_cast<gsize>(next_start_code_index);
        search_offset = nal_end;

        if (nal_end <= nal_start + 2) {
            continue;
        }

        const guint8 nal_type_byte = sf::get_nal_unit_type(data + nal_start, nal_end - nal_start);
        if (nal_type_byte == 0xFFU) {
            continue;
        }
        const guint nal_type = static_cast<guint>(nal_type_byte);

        if (nal_type == k_h265_nal_sps && stream_id != nullptr) {
            const guint8 *sps_data = data + nal_start;
            const gsize sps_length = nal_end - nal_start;
            const std::optional<sf::h265_sps_qp_info> qp_info = sf::parse_sps_for_qp(sps_data, sps_length);
            if (qp_info.has_value() && qp_info->valid) {
                const guint base_qp = static_cast<guint>(qp_info->base_qp);
                sf_sei_sync_store_put_stream_base_qp(stream_id, base_qp);
            }
        }

        if (nal_type != k_h265_nal_prefix_sei && nal_type != k_h265_nal_suffix_sei) {
            continue;
        }

        const gsize payload_offset = nal_start + 2;
        const gsize payload_size = nal_end - payload_offset;
        gsize rbsp_size = 0;
        guint8 *rbsp = sf_rbsp_from_ebsp(data + payload_offset, payload_size, &rbsp_size);
        if (rbsp == nullptr || rbsp_size == 0) {
            g_free(rbsp);
            continue;
        }

        sf_parse_sei_rbsp_messages(rbsp, rbsp_size, pri_payload, eoi_payload);
        g_free(rbsp);
    }
}

static gboolean sf_h265_sei_extract_set_caps(GstBaseTransform *base_transform, GstCaps *in_caps, GstCaps *out_caps) {
    (void) base_transform;
    (void) in_caps;
    (void) out_caps;
    return TRUE;
}

static GstCaps *sf_h265_sei_extract_transform_caps(GstBaseTransform *base_transform, GstPadDirection direction,
                                                   GstCaps *caps, GstCaps *filter) {
    GstCaps *result = nullptr;
    (void) base_transform;
    (void) direction;

    result = gst_caps_ref(caps);
    if (filter != nullptr) {
        GstCaps *intersected = gst_caps_intersect_full(result, filter, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = intersected;
    }

    return result;
}

static GstFlowReturn sf_h265_sei_extract_transform_ip(GstBaseTransform *base_transform, GstBuffer *buffer) {
    sf_h265_sei_extract *self = SF_H265_SEI_EXTRACT(base_transform);
    GstMapInfo map_info;
    sf_payload_blob pri_payload = {nullptr, 0};
    sf_payload_blob eoi_payload = {nullptr, 0};
    sf_frame_identity identity = {0};
    gboolean map_ok = FALSE;

    map_ok = gst_buffer_map(buffer, &map_info, GST_MAP_READ);
    if (!map_ok) {
        GST_WARNING_OBJECT(base_transform, "Failed to map input H.265 buffer");
        return GST_FLOW_OK;
    }

    (void) sf_frame_identity_from_buffer(base_transform, buffer, &identity);
    const gchar *stream_id = identity.valid ? identity.stream_id : nullptr;
    sf_extract_pri_eoi_from_annexb(map_info.data, map_info.size, &pri_payload, &eoi_payload, stream_id);
    gst_buffer_unmap(buffer, &map_info);

    if (pri_payload.size > 0 || eoi_payload.size > 0) {
        if (identity.valid) {
            if (!sf_sei_sync_store_put(identity.stream_id, identity.key_kind, identity.key_value, pri_payload.data,
                                       pri_payload.size, eoi_payload.data, eoi_payload.size)) {
                ++self->publish_failures;
                GST_WARNING_OBJECT(base_transform, "Failed to publish PRI/EOI payloads into sync store");
            } else {
                ++self->produced_entries;
            }
        } else {
            ++self->publish_failures;
            GST_WARNING_OBJECT(base_transform, "Missing frame identity; PRI/EOI sync-store publish skipped");
        }

        if (sf_buffer_add_sei_meta(buffer, pri_payload.data, pri_payload.size, eoi_payload.data, eoi_payload.size) ==
            nullptr) {
            GST_WARNING_OBJECT(base_transform, "Failed to attach VCAI SEI metadata");
        } else {
            GST_LOG_OBJECT(base_transform,
                           "Attached VCAI SEI metadata: PRI=%" G_GSIZE_FORMAT " bytes, EOI=%" G_GSIZE_FORMAT
                           " bytes",
                           pri_payload.size, eoi_payload.size);
        }

        if (self->produced_entries == 1U || (self->produced_entries % 200U) == 0U) {
            sf_sei_sync_store_stats stats = {0};
            sf_sei_sync_store_get_stats(&stats);
            GST_INFO_OBJECT(base_transform,
                            "SEI sync-store producer stats: produced=%" G_GUINT64_FORMAT
                            " publish_failures=%" G_GUINT64_FORMAT " store_size=%u evictions=%" G_GUINT64_FORMAT,
                            self->produced_entries, self->publish_failures, stats.size, stats.evictions);
        }
    }

    sf_frame_identity_clear(&identity);
    sf_payload_blob_reset(&pri_payload);
    sf_payload_blob_reset(&eoi_payload);
    return GST_FLOW_OK;
}

static void sf_h265_sei_extract_class_init(sf_h265_sei_extractClass *klass) {
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gst_element_class_set_static_metadata(element_class, "VCAI H.265 SEI Extract", "Codec/Parser/Video/Metadata",
                                          "Extracts VCAI SEI metadata from H.265 access units", "VCAI");
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    transform_class->set_caps = GST_DEBUG_FUNCPTR(sf_h265_sei_extract_set_caps);
    transform_class->transform_caps = GST_DEBUG_FUNCPTR(sf_h265_sei_extract_transform_caps);
    transform_class->transform_ip = GST_DEBUG_FUNCPTR(sf_h265_sei_extract_transform_ip);
    transform_class->passthrough_on_same_caps = FALSE;
    transform_class->transform_ip_on_passthrough = FALSE;
}

static void sf_h265_sei_extract_init(sf_h265_sei_extract *self) {
    GST_DEBUG_CATEGORY_INIT(sf_h265_sei_extract_category, "sf-h265-sei-extract", 0,
                            "VCAI H.265 PRI/EOI extraction");
    self->produced_entries = 0U;
    self->publish_failures = 0U;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}
