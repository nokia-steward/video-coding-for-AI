// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "core/sf_frame_identity.h"

void sf_frame_identity_clear(sf_frame_identity *identity) {
    if (identity == nullptr) {
        return;
    }
    g_free(identity->stream_id);
    identity->stream_id = nullptr;
    identity->key_kind = SF_SEI_KEY_KIND_PTS;
    identity->key_value = 0U;
    identity->valid = FALSE;
}

gboolean sf_frame_identity_from_buffer(GstBaseTransform *base_transform, GstBuffer *buffer,
                                       sf_frame_identity *out_identity) {
    GstPad *sink_pad = nullptr;
    gchar *stream_id = nullptr;

    if (base_transform == nullptr || buffer == nullptr || out_identity == nullptr) {
        return FALSE;
    }

    sf_frame_identity_clear(out_identity);

    sink_pad = GST_BASE_TRANSFORM_SINK_PAD(base_transform);
    if (sink_pad != nullptr) {
        stream_id = gst_pad_get_stream_id(sink_pad);
    }

    out_identity->stream_id = (stream_id != nullptr) ? stream_id : g_strdup("");
    if (out_identity->stream_id == nullptr) {
        return FALSE;
    }

    if (GST_BUFFER_PTS_IS_VALID(buffer)) {
        out_identity->key_kind = SF_SEI_KEY_KIND_PTS;
        out_identity->key_value = GST_BUFFER_PTS(buffer);
        out_identity->valid = TRUE;
        return TRUE;
    }

    if (GST_BUFFER_DTS_IS_VALID(buffer)) {
        out_identity->key_kind = SF_SEI_KEY_KIND_DTS;
        out_identity->key_value = GST_BUFFER_DTS(buffer);
        out_identity->valid = TRUE;
        return TRUE;
    }

    if (GST_BUFFER_OFFSET_IS_VALID(buffer)) {
        out_identity->key_kind = SF_SEI_KEY_KIND_OFFSET;
        out_identity->key_value = GST_BUFFER_OFFSET(buffer);
        out_identity->valid = TRUE;
        return TRUE;
    }

    out_identity->valid = FALSE;
    return TRUE;
}
