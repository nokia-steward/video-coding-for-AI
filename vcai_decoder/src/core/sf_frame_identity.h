// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "core/sf_sei_sync_store.h"

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

typedef struct {
    gchar *stream_id;
    sf_sei_key_kind key_kind;
    guint64 key_value;
    gboolean valid;
} sf_frame_identity;

/** @brief Clears \a identity and frees owned stream_id. */
void sf_frame_identity_clear(sf_frame_identity *identity);

/**
 * @brief Fills \a out_identity from \a buffer using PTS, DTS, or offset.
 * @param base_transform GStreamer base transform (used to get sink pad stream-id).
 * @param buffer Buffer to read timing from.
 * @param out_identity Output identity; cleared then filled on success.
 * @return TRUE if a valid key (PTS/DTS/offset) was found and \a out_identity is valid.
 */
[[nodiscard]] gboolean sf_frame_identity_from_buffer(GstBaseTransform *base_transform, GstBuffer *buffer,
                                                     sf_frame_identity *out_identity);

G_END_DECLS
