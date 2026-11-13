// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ops/sf_tet_tiles.h"

#include <gst_wrappers.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    gchar magic[4];
    guint32 version;
    guint32 num_qps;
    guint32 num_channels;
    guint32 width;
    guint32 height;
    guint32 normalization_type;
    gfloat scale_factor;
    gfloat data_min;
    gfloat data_max;
} sf_tet_tile_header_raw;

enum { SF_TET_NORMALIZATION_NORMALIZED = 0, SF_TET_NORMALIZATION_RAW_DIFF = 1, SF_TET_NORMALIZATION_CENTERED = 2 };

static gfloat sf_tet_normalization_scale(guint32 normalization_type) {
    if (normalization_type == SF_TET_NORMALIZATION_NORMALIZED || normalization_type == SF_TET_NORMALIZATION_CENTERED) {
        return 255.0f;
    }
    return 1.0f;
}

void sf_tet_tile_set_clear(sf_tet_tile_set *tiles) {
    if (tiles == nullptr) {
        return;
    }

    if (tiles->tiles != nullptr) {
        for (guint32 index = 0U; index < tiles->num_qps; ++index) {
            g_free(tiles->tiles[index]);
        }
        g_free(tiles->tiles);
    }
    g_free(tiles->qp_values);

    memset(tiles, 0, sizeof(*tiles));
}

gboolean sf_tet_tile_set_load(const gchar *path, sf_tet_tile_set *out_tiles) {
    FILE *file = nullptr;
    sf_tet_tile_header_raw raw_header = {0};
    gsize tile_element_count = 0U;
    gfloat total_scale = 0.0f;

    if (path == nullptr || out_tiles == nullptr) {
        return FALSE;
    }

    sf_tet_tile_set_clear(out_tiles);

    file = fopen(path, "rb");
    if (file == nullptr) {
        return FALSE;
    }

    if (fread(&raw_header, sizeof(raw_header), 1U, file) != 1U) {
        /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
        fclose(file);
        return FALSE;
    }

    if (memcmp(raw_header.magic, "TET\0", 4U) != 0 || raw_header.version != 1U || raw_header.num_qps == 0U ||
        !sf::is_valid_video_dimensions(static_cast<int>(raw_header.width), static_cast<int>(raw_header.height)) ||
        (raw_header.num_channels != 1U && raw_header.num_channels != 3U)) {
        /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
        fclose(file);
        return FALSE;
    }

    out_tiles->version = raw_header.version;
    out_tiles->num_qps = raw_header.num_qps;
    out_tiles->num_channels = raw_header.num_channels;
    out_tiles->width = raw_header.width;
    out_tiles->height = raw_header.height;
    out_tiles->normalization_type = raw_header.normalization_type;
    out_tiles->scale_factor = raw_header.scale_factor;
    out_tiles->data_min = raw_header.data_min;
    out_tiles->data_max = raw_header.data_max;

    /* RAII_EXEMPT: GLib g_new0; ownership transferred to out_tiles, cleared in sf_tet_tile_set_clear. */
    out_tiles->qp_values = static_cast<guint32 *>(g_new0(guint32, out_tiles->num_qps));
    if (out_tiles->qp_values == nullptr) {
        /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
        fclose(file);
        sf_tet_tile_set_clear(out_tiles);
        return FALSE;
    }

    if (fread(out_tiles->qp_values, sizeof(guint32), out_tiles->num_qps, file) != out_tiles->num_qps) {
        /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
        fclose(file);
        sf_tet_tile_set_clear(out_tiles);
        return FALSE;
    }

    tile_element_count = static_cast<gsize>(out_tiles->width) * static_cast<gsize>(out_tiles->height) *
                         static_cast<gsize>(out_tiles->num_channels);
    out_tiles->tile_element_count = tile_element_count;
    /* RAII_EXEMPT: GLib g_new0; ownership in out_tiles->tiles, cleared in sf_tet_tile_set_clear. */
    out_tiles->tiles = static_cast<gint16 **>(g_new0(gint16 *, out_tiles->num_qps));
    if (out_tiles->tiles == nullptr) {
        /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
        fclose(file);
        sf_tet_tile_set_clear(out_tiles);
        return FALSE;
    }

    total_scale = out_tiles->scale_factor * sf_tet_normalization_scale(out_tiles->normalization_type);
    for (guint32 qp_index = 0U; qp_index < out_tiles->num_qps; ++qp_index) {
        /* RAII_EXEMPT: GLib g_new0; ownership in out_tiles->tiles[qp_index], cleared in sf_tet_tile_set_clear. */
        out_tiles->tiles[qp_index] = static_cast<gint16 *>(g_new0(gint16, tile_element_count));
        if (out_tiles->tiles[qp_index] == nullptr) {
            /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
            fclose(file);
            sf_tet_tile_set_clear(out_tiles);
            return FALSE;
        }

        for (gsize value_index = 0U; value_index < tile_element_count; ++value_index) {
            gfloat raw_value = 0.0f;
            gint32 scaled = 0;
            if (fread(&raw_value, sizeof(gfloat), 1U, file) != 1U) {
                /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
                fclose(file);
                sf_tet_tile_set_clear(out_tiles);
                return FALSE;
            }
            scaled = static_cast<gint32>(lroundf(raw_value * total_scale));
            out_tiles->tiles[qp_index][value_index] = static_cast<gint16>(CLAMP(scaled, G_MININT16, G_MAXINT16));
        }
    }

    /* RAII_EXEMPT: FILE* owned by this scope; fclose releases it. */
    fclose(file);
    return TRUE;
}

guint sf_tet_find_tile_index_for_qp(const sf_tet_tile_set *tiles, const guint qp) {
    guint best_index = 0U;
    guint best_delta = G_MAXUINT;

    if (tiles == nullptr || tiles->num_qps == 0U || tiles->qp_values == nullptr) {
        return 0U;
    }

    for (guint index = 0U; index < tiles->num_qps; ++index) {
        const guint candidate_qp = tiles->qp_values[index];
        const guint delta = (candidate_qp > qp) ? (candidate_qp - qp) : (qp - candidate_qp);
        if (delta < best_delta || (delta == best_delta && candidate_qp < tiles->qp_values[best_index])) {
            best_delta = delta;
            best_index = index;
        }
    }
    return best_index;
}

/** @brief Adds tile_data (tiled) to plane_data in place with clamping to [0,255]. Used per I420 plane. */
static gboolean sf_apply_channel(guint8 *plane_data, guint plane_stride, gint plane_width, gint plane_height,
                                 const gint16 *tile_data, guint tile_width, guint tile_height) {
    if (plane_data == nullptr || tile_data == nullptr || plane_width <= 0 || plane_height <= 0 || tile_width == 0U ||
        tile_height == 0U) {
        return FALSE;
    }

    for (gint row = 0; row < plane_height; ++row) {
        const guint tile_row = static_cast<guint>(row) % tile_height;
        guint8 *row_ptr = plane_data + (static_cast<gsize>(row) * static_cast<gsize>(plane_stride));
        for (gint col = 0; col < plane_width; ++col) {
            const guint tile_col = static_cast<guint>(col) % tile_width;
            const gsize tile_index = (static_cast<gsize>(tile_row) * tile_width) + tile_col;
            const gint value = static_cast<gint>(row_ptr[col]) + static_cast<gint>(tile_data[tile_index]);
            row_ptr[col] = static_cast<guint8>(CLAMP(value, 0, 255));
        }
    }

    return TRUE;
}

gboolean sf_tet_apply_i420_in_place(GstBuffer *buffer, const GstVideoInfo *video_info, const sf_tet_tile_set *tiles,
                                    guint tile_index) {
    GstMapInfo map = {0};

    if (buffer == nullptr || video_info == nullptr || tiles == nullptr || tile_index >= tiles->num_qps ||
        tiles->tiles == nullptr || tiles->tiles[tile_index] == nullptr ||
        GST_VIDEO_INFO_FORMAT(video_info) != GST_VIDEO_FORMAT_I420) {
        return FALSE;
    }

    if (!gst_buffer_map(buffer, &map, GST_MAP_READWRITE)) {
        return FALSE;
    }

    for (guint plane_index = 0U; plane_index < static_cast<guint>(GST_VIDEO_INFO_N_PLANES(video_info)); ++plane_index) {
        const guint channel_index = plane_index;
        guint tile_width = tiles->width;
        guint tile_height = tiles->height;
        const gint plane_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, static_cast<gint>(plane_index));
        const gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, static_cast<gint>(plane_index));
        const guint plane_stride =
                static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, static_cast<gint>(plane_index)));
        guint8 *plane_data = map.data + GST_VIDEO_INFO_PLANE_OFFSET(video_info, static_cast<gint>(plane_index));
        const gint16 *channel_tile = nullptr;

        if (plane_index > 0U && tiles->num_channels == 1U) {
            continue;
        }
        if (channel_index >= tiles->num_channels) {
            gst_buffer_unmap(buffer, &map);
            return FALSE;
        }

        if (plane_index > 0U) {
            tile_width = tiles->width / 2U;
            tile_height = tiles->height / 2U;
            if (tile_width == 0U || tile_height == 0U) {
                gst_buffer_unmap(buffer, &map);
                return FALSE;
            }
        }

        channel_tile =
                tiles->tiles[tile_index] + (static_cast<gsize>(channel_index) * static_cast<gsize>(tiles->width) *
                                            static_cast<gsize>(tiles->height));
        if (!sf_apply_channel(plane_data, plane_stride, plane_width, plane_height, channel_tile, tile_width,
                              tile_height)) {
            gst_buffer_unmap(buffer, &map);
            return FALSE;
        }
    }

    gst_buffer_unmap(buffer, &map);
    return TRUE;
}
