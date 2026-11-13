// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct {
    guint32 version;
    guint32 num_qps;
    guint32 num_channels;
    guint32 width;
    guint32 height;
    guint32 normalization_type;
    gfloat scale_factor;
    gfloat data_min;
    gfloat data_max;
    guint32 *qp_values;
    gint16 **tiles;
    gsize tile_element_count;
} sf_tet_tile_set;

/** @brief Frees qp_values, tiles, and zeroes \a tiles. Safe if \a tiles is nullptr. */
void sf_tet_tile_set_clear(sf_tet_tile_set *tiles);

/**
 * @brief Loads a .tet file into \a out_tiles.
 * @param path Path to the .tet file.
 * @param out_tiles Cleared then filled; call sf_tet_tile_set_clear when done.
 * @return TRUE on success.
 */
[[nodiscard]] gboolean sf_tet_tile_set_load(const gchar *path, sf_tet_tile_set *out_tiles);

/**
 * @brief Returns the tile index whose QP is closest to \a qp.
 * @param tiles Loaded tile set.
 * @param qp Quantization parameter.
 * @return Index in [0, tiles->num_qps), or 0 if invalid input.
 */
[[nodiscard]] guint sf_tet_find_tile_index_for_qp(const sf_tet_tile_set *tiles, guint qp);

/**
 * @brief Applies the selected TET tile luma modulation to \a buffer (I420) in place.
 * @param buffer Mapped I420 buffer.
 * @param video_info Format of \a buffer.
 * @param tiles Loaded tile set.
 * @param tile_index Index from sf_tet_find_tile_index_for_qp.
 * @return TRUE on success.
 */
[[nodiscard]] gboolean sf_tet_apply_i420_in_place(GstBuffer *buffer, const GstVideoInfo *video_info,
                                                  const sf_tet_tile_set *tiles, guint tile_index);

G_END_DECLS
