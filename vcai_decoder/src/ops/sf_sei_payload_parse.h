// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct {
    gboolean present;
    gboolean parse_ok;
    gboolean cancel_flag;
    guint region_count;
} sf_pri_payload_info;

typedef struct {
    gboolean present;
    gboolean parse_ok;
    gboolean cancel_flag;
    guint16 type_bits;
    gboolean has_luma_adaptation;
    guint luma_adaptation_idc;
    guint ratio_luma_value;
    gboolean ratio_luma_sign_flag;
    guint backscale_ratio_value;
} sf_eoi_payload_info;

typedef struct {
    sf_pri_payload_info pri;
    sf_eoi_payload_info eoi;
} sf_sei_payload_parse_result;

/**
 * @brief Parses a PRI SEI payload (summary only: present/parse_ok/cancel_flag/region_count).
 *
 * Uses @c sf::read_packed_regions_info as the bitstream source of truth. @a region_count is the number of
 * non-layer regions with full packed geometry (same cardinality as the former detailed @c GArray).
 *
 * @param data Payload bytes (after payload type/size in SEI NAL).
 * @param size Payload size.
 * @param out_info Filled with present/parse_ok/cancel_flag/region_count.
 * @return TRUE if parsing completed (check out_info->parse_ok for validity); FALSE on truncated/invalid RBSP when
 *         size is non-zero.
 */
[[nodiscard]] gboolean sf_parse_pri_payload(const guint8 *data, gsize size, sf_pri_payload_info *out_info);

/**
 * @brief Parses an EOI SEI payload.
 * @param data Payload bytes.
 * @param size Payload size.
 * @param out_info Filled with present/parse_ok and EOI-specific fields.
 * @return TRUE if parsing completed (check out_info->parse_ok for validity).
 */
[[nodiscard]] gboolean sf_parse_eoi_payload(const guint8 *data, gsize size, sf_eoi_payload_info *out_info);

G_END_DECLS
