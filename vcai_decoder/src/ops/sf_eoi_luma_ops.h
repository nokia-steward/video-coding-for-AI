// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "ops/sf_sei_payload_parse.h"

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * @brief Computes the luma backscale multiplier from parsed EOI payload.
 * @param eoi Parsed EOI payload (must have parse_ok and luma adaptation).
 * @param out_multiplier Output multiplier (backscale_ratio / ratio_percentage).
 * @return TRUE if multiplier was computed and is positive.
 */
[[nodiscard]] gboolean sf_compute_eoi_luma_multiplier(const sf_eoi_payload_info *eoi, gdouble *out_multiplier);

/**
 * @brief Applies a luma multiplier to the Y plane of \a buffer in place.
 * @param buffer Mapped video buffer (I420).
 * @param video_info Format of \a buffer.
 * @param multiplier Scale factor for luma (e.g. from sf_compute_eoi_luma_multiplier).
 * @return TRUE on success.
 */
[[nodiscard]] gboolean sf_apply_luma_multiplier_in_place(GstBuffer *buffer, const GstVideoInfo *video_info,
                                                         gdouble multiplier);

G_END_DECLS
