// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "core/sf_sei_sync_store.h"

typedef struct {
    gchar *key;
    gint64 inserted_usec;
    guint8 *pri_data;
    gsize pri_size;
    guint8 *eoi_data;
    gsize eoi_size;
} sf_sei_store_entry;

typedef struct {
    GHashTable *entries;
    GHashTable *stream_base_qp; /* stream_id -> GUINT_TO_POINTER(base_qp) */
    GMutex mutex;
    guint max_entries;
    gint64 max_age_usec;
    guint64 puts;
    guint64 lookups;
    guint64 lookup_hits;
    guint64 consumes;
    guint64 consume_hits;
    guint64 evictions;
    guint64 expired_evictions;
} sf_sei_store;

static sf_sei_store *sf_get_store(void);

/** @brief GHashTable value destructor; frees key, pri_data, eoi_data, and the entry. */
static void sf_store_entry_free(gpointer data) {
    sf_sei_store_entry *entry = static_cast<sf_sei_store_entry *>(data);
    if (entry == nullptr) {
        return;
    }
    g_free(entry->key);
    g_free(entry->pri_data);
    g_free(entry->eoi_data);
    g_free(entry);
}

static sf_sei_store *sf_get_store(void) {
    static sf_sei_store store = {0};
    static gsize initialized = 0;

    if (g_once_init_enter(&initialized)) {
        g_mutex_init(&store.mutex);
        store.entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, sf_store_entry_free);
        store.stream_base_qp = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
        store.max_entries = 2048U;
        store.max_age_usec = 5 * G_USEC_PER_SEC;
        g_once_init_leave(&initialized, 1);
    }
    return &store;
}

/** @brief Builds a string key for the sync store from stream_id, key_kind, key_value. Caller frees. */
static gchar *sf_make_store_key(const gchar *stream_id, sf_sei_key_kind key_kind, guint64 key_value) {
    const gchar *safe_stream_id = (stream_id != nullptr) ? stream_id : "";
    return g_strdup_printf("%s|%u|%" G_GUINT64_FORMAT, safe_stream_id, static_cast<guint>(key_kind), key_value);
}

/** @brief Replaces *dst_data with a copy of src_data; frees old *dst_data. @return TRUE on success. */
static gboolean sf_copy_blob(guint8 **dst_data, gsize *dst_size, const guint8 *src_data, gsize src_size) {
    guint8 *copy = nullptr;

    if (src_size > 0U) {
        if (src_data == nullptr) {
            return FALSE;
        }
        copy = static_cast<guint8 *>(g_memdup2(src_data, src_size));
        if (copy == nullptr) {
            return FALSE;
        }
    }

    g_free(*dst_data);
    *dst_data = copy;
    *dst_size = src_size;
    return TRUE;
}

/** @brief Copies entry's pri/eoi blobs into out_payload. If out_payload is nullptr, returns TRUE. */
static gboolean sf_copy_payload_to_output(const sf_sei_store_entry *entry, sf_sei_payload_owned *out_payload) {
    if (out_payload == nullptr) {
        return TRUE;
    }

    if (!sf_copy_blob(&out_payload->pri_data, &out_payload->pri_size, entry->pri_data, entry->pri_size)) {
        return FALSE;
    }

    if (!sf_copy_blob(&out_payload->eoi_data, &out_payload->eoi_size, entry->eoi_data, entry->eoi_size)) {
        return FALSE;
    }

    return TRUE;
}

/** @brief Removes the oldest entry by inserted_usec; updates eviction counters. Call with store locked. */
static gboolean sf_remove_oldest_entry_locked(sf_sei_store *store, gboolean count_as_expired) {
    GHashTableIter iter;
    gpointer table_key = nullptr;
    gpointer table_value = nullptr;
    gchar *oldest_key = nullptr;
    gint64 oldest_inserted_usec = G_MAXINT64;

    g_hash_table_iter_init(&iter, store->entries);
    while (g_hash_table_iter_next(&iter, &table_key, &table_value)) {
        const sf_sei_store_entry *entry = static_cast<const sf_sei_store_entry *>(table_value);
        if (entry->inserted_usec < oldest_inserted_usec) {
            oldest_inserted_usec = entry->inserted_usec;
            oldest_key = static_cast<gchar *>(table_key);
        }
    }

    if (oldest_key == nullptr) {
        return FALSE;
    }

    if (g_hash_table_remove(store->entries, oldest_key)) {
        ++store->evictions;
        if (count_as_expired) {
            ++store->expired_evictions;
        }
        return TRUE;
    }

    return FALSE;
}

/** @brief Removes entries older than max_age_usec. Call with store locked. */
static void sf_evict_expired_locked(sf_sei_store *store, gint64 now_usec) {
    if (store->max_age_usec <= 0) {
        return;
    }

    while (g_hash_table_size(store->entries) > 0U) {
        GHashTableIter iter;
        gpointer table_key = nullptr;
        gpointer table_value = nullptr;
        gboolean removed_any = FALSE;

        g_hash_table_iter_init(&iter, store->entries);
        while (g_hash_table_iter_next(&iter, &table_key, &table_value)) {
            const sf_sei_store_entry *entry = static_cast<const sf_sei_store_entry *>(table_value);
            if ((now_usec - entry->inserted_usec) > store->max_age_usec) {
                g_hash_table_iter_remove(&iter);
                ++store->evictions;
                ++store->expired_evictions;
                removed_any = TRUE;
                break;
            }
        }

        if (!removed_any) {
            break;
        }
    }
}

/** @brief Evicts oldest entries until size <= max_entries. Call with store locked. */
static void sf_enforce_size_limit_locked(sf_sei_store *store) {
    while (g_hash_table_size(store->entries) > store->max_entries) {
        if (!sf_remove_oldest_entry_locked(store, FALSE)) {
            break;
        }
    }
}

void sf_sei_payload_owned_clear(sf_sei_payload_owned *payload) {
    if (payload == nullptr) {
        return;
    }
    g_free(payload->pri_data);
    g_free(payload->eoi_data);
    payload->pri_data = nullptr;
    payload->pri_size = 0U;
    payload->eoi_data = nullptr;
    payload->eoi_size = 0U;
}

void sf_sei_sync_store_set_limits(const guint max_entries, const gint64 max_age_usec) {
    sf_sei_store *store = sf_get_store();

    g_mutex_lock(&store->mutex);
    store->max_entries = (max_entries == 0U) ? 1U : max_entries;
    store->max_age_usec = max_age_usec;
    sf_enforce_size_limit_locked(store);
    g_mutex_unlock(&store->mutex);
}

gboolean sf_sei_sync_store_put(const gchar *stream_id, const sf_sei_key_kind key_kind, const guint64 key_value,
                               const guint8 *pri_data, const gsize pri_size, const guint8 *eoi_data,
                               const gsize eoi_size) {
    sf_sei_store *store = sf_get_store();
    sf_sei_store_entry *entry = nullptr;
    gchar *key = nullptr;
    gint64 now_usec = 0;

    if (key_kind != SF_SEI_KEY_KIND_PTS && key_kind != SF_SEI_KEY_KIND_DTS && key_kind != SF_SEI_KEY_KIND_OFFSET) {
        return FALSE;
    }
    if (pri_size > 0U && pri_data == nullptr) {
        return FALSE;
    }
    if (eoi_size > 0U && eoi_data == nullptr) {
        return FALSE;
    }

    key = sf_make_store_key(stream_id, key_kind, key_value);
    if (key == nullptr) {
        return FALSE;
    }

    entry = static_cast<sf_sei_store_entry *>(g_new0(sf_sei_store_entry, 1U));
    if (entry == nullptr) {
        g_free(key);
        return FALSE;
    }

    entry->key = g_strdup(key);
    if (entry->key == nullptr) {
        g_free(key);
        sf_store_entry_free(entry);
        return FALSE;
    }

    if (!sf_copy_blob(&entry->pri_data, &entry->pri_size, pri_data, pri_size) ||
        !sf_copy_blob(&entry->eoi_data, &entry->eoi_size, eoi_data, eoi_size)) {
        g_free(key);
        sf_store_entry_free(entry);
        return FALSE;
    }

    now_usec = g_get_monotonic_time();
    entry->inserted_usec = now_usec;

    g_mutex_lock(&store->mutex);
    ++store->puts;
    sf_evict_expired_locked(store, now_usec);
    g_hash_table_replace(store->entries, key, entry);
    sf_enforce_size_limit_locked(store);
    g_mutex_unlock(&store->mutex);
    return TRUE;
}

gboolean sf_sei_sync_store_lookup(const gchar *stream_id, const sf_sei_key_kind key_kind, const guint64 key_value,
                                  sf_sei_payload_owned *out_payload) {
    sf_sei_store *store = sf_get_store();
    sf_sei_store_entry *entry = nullptr;
    gchar *key = nullptr;
    gboolean result = FALSE;
    gint64 now_usec = 0;

    if (out_payload != nullptr) {
        sf_sei_payload_owned_clear(out_payload);
    }

    key = sf_make_store_key(stream_id, key_kind, key_value);
    if (key == nullptr) {
        return FALSE;
    }

    g_mutex_lock(&store->mutex);
    ++store->lookups;
    now_usec = g_get_monotonic_time();
    sf_evict_expired_locked(store, now_usec);
    entry = static_cast<sf_sei_store_entry *>(g_hash_table_lookup(store->entries, key));
    if (entry != nullptr) {
        result = sf_copy_payload_to_output(entry, out_payload);
        if (result) {
            ++store->lookup_hits;
        }
    }
    g_mutex_unlock(&store->mutex);

    g_free(key);
    return result && entry != nullptr;
}

gboolean sf_sei_sync_store_consume(const gchar *stream_id, const sf_sei_key_kind key_kind, const guint64 key_value,
                                   sf_sei_payload_owned *out_payload) {
    sf_sei_store *store = sf_get_store();
    sf_sei_store_entry *entry = nullptr;
    gchar *key = nullptr;
    gboolean result = FALSE;
    gint64 now_usec = 0;

    if (out_payload != nullptr) {
        sf_sei_payload_owned_clear(out_payload);
    }

    key = sf_make_store_key(stream_id, key_kind, key_value);
    if (key == nullptr) {
        return FALSE;
    }

    g_mutex_lock(&store->mutex);
    ++store->consumes;
    now_usec = g_get_monotonic_time();
    sf_evict_expired_locked(store, now_usec);
    entry = static_cast<sf_sei_store_entry *>(g_hash_table_lookup(store->entries, key));
    if (entry != nullptr) {
        result = sf_copy_payload_to_output(entry, out_payload);
        if (result) {
            ++store->consume_hits;
            g_hash_table_remove(store->entries, key);
        }
    }
    g_mutex_unlock(&store->mutex);

    g_free(key);
    return result && entry != nullptr;
}

void sf_sei_sync_store_get_stats(sf_sei_sync_store_stats *out_stats) {
    sf_sei_store *store = sf_get_store();

    if (out_stats == nullptr) {
        return;
    }

    g_mutex_lock(&store->mutex);
    out_stats->size = static_cast<guint>(g_hash_table_size(store->entries));
    out_stats->max_size = store->max_entries;
    out_stats->puts = store->puts;
    out_stats->lookups = store->lookups;
    out_stats->lookup_hits = store->lookup_hits;
    out_stats->consumes = store->consumes;
    out_stats->consume_hits = store->consume_hits;
    out_stats->evictions = store->evictions;
    out_stats->expired_evictions = store->expired_evictions;
    g_mutex_unlock(&store->mutex);
}

void sf_sei_sync_store_clear(void) {
    sf_sei_store *store = sf_get_store();

    g_mutex_lock(&store->mutex);
    g_hash_table_remove_all(store->entries);
    g_hash_table_remove_all(store->stream_base_qp);
    g_mutex_unlock(&store->mutex);
}

void sf_sei_sync_store_put_stream_base_qp(const gchar *stream_id, guint base_qp) {
    sf_sei_store *store = sf_get_store();
    const gchar *safe_id = (stream_id != nullptr) ? stream_id : "";
    gchar *key = g_strdup(safe_id);
    if (key == nullptr) {
        return;
    }
    const guint clamped = (base_qp > 63U) ? 63U : base_qp;
    g_mutex_lock(&store->mutex);
    g_hash_table_replace(store->stream_base_qp, key, GUINT_TO_POINTER(clamped));
    g_mutex_unlock(&store->mutex);
}

gboolean sf_sei_sync_store_get_stream_base_qp(const gchar *stream_id, guint *out_qp) {
    sf_sei_store *store = sf_get_store();
    const gchar *safe_id = (stream_id != nullptr) ? stream_id : "";
    gpointer value = nullptr;

    if (out_qp == nullptr) {
        return FALSE;
    }
    g_mutex_lock(&store->mutex);
    value = g_hash_table_lookup(store->stream_base_qp, safe_id);
    if (value != nullptr) {
        *out_qp = GPOINTER_TO_UINT(value);
    }
    g_mutex_unlock(&store->mutex);
    return value != nullptr;
}
