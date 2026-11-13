// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <packed_regions_info.h>

typedef struct {
    guint copied_regions;
    guint skipped_regions;
} sf_pri_reconstruct_stats;

/**
 * @brief Copies PRI regions from packed positions to target positions in-place on \a buffer.
 * @param buffer Mapped I420/NV12 buffer (modified in place).
 * @param video_info Format and layout of \a buffer.
 * @param pri Parsed PRI from @c sf::read_packed_regions_info (same semantics as lib reconstruct path).
 * @param out_stats Optional; filled with copied/skipped region counts.
 * @return TRUE on success. Cancel-only PRI returns TRUE with zero copies. FALSE if \a pri is inconsistent for
 *         pixel mapping (non-cancel).
 */
[[nodiscard]] gboolean sf_pri_reconstruct_in_place(GstBuffer *buffer, const GstVideoInfo *video_info,
                                                   const sf::packed_regions_info &pri,
                                                   sf_pri_reconstruct_stats *out_stats);

/**
 * @brief Fills \a output_buffer with background color then copies PRI regions from \a input_buffer.
 */
[[nodiscard]] gboolean sf_pri_reconstruct_to_output(GstBuffer *input_buffer, GstBuffer *output_buffer,
                                                    const GstVideoInfo *video_info, const sf::packed_regions_info &pri,
                                                    guint32 background_color, sf_pri_reconstruct_stats *out_stats);
