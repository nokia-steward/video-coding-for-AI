// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gst/gst.h>
#include <memory>
#include <mutex>
#include <queue>

/**
 * @file ingress_bridge_runtime.h
 * @brief Runtime state for encoder ingress bridge lifecycle and counters.
 */

/**
 * @brief Thread-safe queue used by PTS preservation probes in inference bypass mode.
 */
struct ingress_pts_storage {
    std::mutex mutex{};
    std::queue<GstClockTime> pts_queue{};
};

/**
 * @brief Tracks ingress callback and queue-close lifecycle for one encoder instance.
 */
struct ingress_bridge_runtime {
    std::atomic_uint64_t dropped_raw_sample_count{0};
    std::atomic_uint64_t encoded_sample_log_count{0};
    std::atomic_bool raw_queue_closed{false};
    std::atomic_bool encoded_queue_closed{false};
    std::unique_ptr<ingress_pts_storage> inference_pts_storage{};

    /**
     * @brief Clears per-instance counters and lifecycle flags.
     */
    void reset() {
        dropped_raw_sample_count = 0;
        encoded_sample_log_count = 0;
        raw_queue_closed = false;
        encoded_queue_closed = false;
        clear_pts_storage();
    }

    /**
     * @brief Ensures inference PTS storage exists and returns it.
     * @return PTS storage owned by this runtime instance.
     */
    ingress_pts_storage &ensure_pts_storage() {
        if (!inference_pts_storage) {
            inference_pts_storage = std::make_unique<ingress_pts_storage>();
        }
        return *inference_pts_storage;
    }

    /**
     * @brief Returns PTS queue size for diagnostics.
     * @return Number of pending PTS entries.
     */
    [[nodiscard]] uint64_t pts_queue_size() const {
        if (!inference_pts_storage) {
            return 0;
        }
        std::lock_guard lock(inference_pts_storage->mutex);
        return inference_pts_storage->pts_queue.size();
    }

private:
    void clear_pts_storage() {
        if (!inference_pts_storage) {
            return;
        }

        std::lock_guard lock(inference_pts_storage->mutex);
        while (!inference_pts_storage->pts_queue.empty()) {
            inference_pts_storage->pts_queue.pop();
        }
    }
};
