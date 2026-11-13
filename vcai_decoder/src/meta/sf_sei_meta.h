// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct sf_sei_meta sf_sei_meta;

struct sf_sei_meta {
    GstMeta meta;
    guint8 *pri_data;
    gsize pri_size;
    guint8 *eoi_data;
    gsize eoi_size;
};

/** @brief Returns the GType for the SEI meta API (for gst_buffer_get_meta). */
[[nodiscard]] GType sf_sei_meta_api_get_type(void);

/** @brief Returns the GstMetaInfo for registering/copying SEI meta. */
[[nodiscard]] const GstMetaInfo *sf_sei_meta_get_info(void);

#define SF_SEI_META_API_TYPE (sf_sei_meta_api_get_type())
#define SF_SEI_META_INFO (sf_sei_meta_get_info())
#define sf_buffer_get_sei_meta(b) ((sf_sei_meta *) gst_buffer_get_meta((b), SF_SEI_META_API_TYPE))

/**
 * @brief Adds or updates SEI meta on \a buffer with the given PRI/EOI payloads.
 * @param buffer Writable GstBuffer.
 * @param pri_data PRI payload (may be nullptr if pri_size 0).
 * @param eoi_data EOI payload (may be nullptr if eoi_size 0).
 * @return Pointer to the meta, or nullptr on failure.
 */
[[nodiscard]] sf_sei_meta *sf_buffer_add_sei_meta(GstBuffer *buffer, const guint8 *pri_data, gsize pri_size,
                                                  const guint8 *eoi_data, gsize eoi_size);

G_END_DECLS
