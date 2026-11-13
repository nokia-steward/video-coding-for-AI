// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum { SF_SEI_KEY_KIND_PTS = 1, SF_SEI_KEY_KIND_DTS = 2, SF_SEI_KEY_KIND_OFFSET = 3 } sf_sei_key_kind;

typedef struct {
    guint8 *pri_data;
    gsize pri_size;
    guint8 *eoi_data;
    gsize eoi_size;
} sf_sei_payload_owned;

typedef struct {
    guint size;
    guint max_size;
    guint64 puts;
    guint64 lookups;
    guint64 lookup_hits;
    guint64 consumes;
    guint64 consume_hits;
    guint64 evictions;
    guint64 expired_evictions;
} sf_sei_sync_store_stats;

/** @brief Frees pri_data/eoi_data in \a payload and zeroes sizes. Safe if \a payload is nullptr. */
void sf_sei_payload_owned_clear(sf_sei_payload_owned *payload);

/** @brief Sets store capacity and max entry age. Affects eviction and lookup behavior. */
void sf_sei_sync_store_set_limits(guint max_entries, gint64 max_age_usec);

/**
 * @brief Stores PRI/EOI payload for a given stream key.
 * @param stream_id Stream identifier (e.g. from pad stream-id).
 * @param key_kind One of PTS, DTS, or OFFSET.
 * @param key_value PTS, DTS, or offset value.
 * @param pri_data PRI payload bytes (may be nullptr if pri_size 0).
 * @param eoi_data EOI payload bytes (may be nullptr if eoi_size 0).
 * @return TRUE on success.
 */
[[nodiscard]] gboolean sf_sei_sync_store_put(const gchar *stream_id, sf_sei_key_kind key_kind, guint64 key_value,
                                             const guint8 *pri_data, gsize pri_size, const guint8 *eoi_data,
                                             gsize eoi_size);

/**
 * @brief Looks up stored payload for a stream key without removing it.
 * @param out_payload Filled with a copy of the payload; cleared first. May be nullptr to only check existence.
 * @return TRUE if an entry was found (and copied into \a out_payload if non-nullptr).
 */
[[nodiscard]] gboolean sf_sei_sync_store_lookup(const gchar *stream_id, sf_sei_key_kind key_kind, guint64 key_value,
                                                sf_sei_payload_owned *out_payload);

/**
 * @brief Looks up and removes stored payload for a stream key.
 * @param out_payload Filled with a copy of the payload; cleared first. May be nullptr.
 * @return TRUE if an entry was found and removed (and copied into \a out_payload if non-nullptr).
 */
[[nodiscard]] gboolean sf_sei_sync_store_consume(const gchar *stream_id, sf_sei_key_kind key_kind, guint64 key_value,
                                                 sf_sei_payload_owned *out_payload);

/** @brief Writes current store size, limits, and counters into \a out_stats. */
void sf_sei_sync_store_get_stats(sf_sei_sync_store_stats *out_stats);

/** @brief Removes all entries from the sync store. */
void sf_sei_sync_store_clear(void);

/**
 * @brief Stores the base QP derived from SPS for a stream (used for dynamic QP in TET).
 * @param stream_id Stream identifier (e.g. from frame identity).
 * @param base_qp Base QP from sf::parse_sps_for_qp (26 + init_qp_minus26).
 */
void sf_sei_sync_store_put_stream_base_qp(const gchar *stream_id, guint base_qp);

/**
 * @brief Retrieves the current stream base QP if set.
 * @param stream_id Stream identifier.
 * @param out_qp Filled with the stored base QP (clamped to 0–63).
 * @return TRUE if a value was found and \a out_qp was set.
 */
[[nodiscard]] gboolean sf_sei_sync_store_get_stream_base_qp(const gchar *stream_id, guint *out_qp);

G_END_DECLS
