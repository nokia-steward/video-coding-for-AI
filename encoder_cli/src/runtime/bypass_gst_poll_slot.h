// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file bypass_gst_poll_slot.h
 * @brief RAII worker slot for bypass appsink polling.
 */

/**
 * @brief Owns one GstTask worker and its recursive mutex for bypass appsink polling.
 *
 * Replaces a raw `std::jthread` for paths that map cleanly onto GStreamer task scheduling.
 */
struct bypass_gst_poll_slot {
    GstTask *task = nullptr;
    GRecMutex lock{};
    bypass_gst_poll_slot() { g_rec_mutex_init(&lock); }
    ~bypass_gst_poll_slot() {
        g_assert(task == nullptr);
        g_rec_mutex_clear(&lock);
    }
    bypass_gst_poll_slot(const bypass_gst_poll_slot &) = delete;
    bypass_gst_poll_slot &operator=(const bypass_gst_poll_slot &) = delete;
};
