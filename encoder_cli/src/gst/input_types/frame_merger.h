// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file frame_merger.h
 * @brief Declares the frame merger for combining frames from inference and bypass paths.
 */

/**
 * @brief Initializes the frame merger debug category.
 */
void init_frame_merger_debug_category();

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>

#include "../../../../libvcai/include/gst_raii.h"

/**
 * @brief Identifies which pipeline path a frame traveled through.
 */
enum class frame_path_type {
    inference, ///< Frame went through DeepStream inference
    bypass ///< Frame bypassed DeepStream inference
};

/**
 * @brief A frame with its path metadata, ready for ordered output.
 */
struct merged_frame {
    sf::unique_gst_sample_ptr sample{};
    frame_path_type path{frame_path_type::inference};
    GstClockTime pts{GST_CLOCK_TIME_NONE};
};

/**
 * @brief Statistics for monitoring frame merger performance.
 */
struct frame_merger_statistics {
    guint64 frames_from_inference = 0;
    guint64 frames_from_bypass = 0;
    guint64 frames_output = 0;
    guint64 reorder_events = 0; ///< Times frames arrived out of PTS order
    guint64 timeout_events = 0; ///< Times timeout forced frame output
    size_t peak_buffer_size = 0; ///< Maximum observed buffer occupancy
};

/**
 * @brief Configuration for the frame merger.
 */
struct frame_merger_config {
    size_t max_buffer_frames = 10; ///< Maximum frames to buffer
    uint32_t max_latency_ms = 100; ///< Maximum wait time before forcing output
    size_t emergency_drop_threshold = 15; ///< Force output when buffer exceeds this
};

/**
 * @brief Reorder buffer that merges frames from dual appsinks by PTS.
 *
 * The frame merger collects frames from both the inference and bypass appsinks,
 * reorders them by presentation timestamp (PTS), and outputs them in the correct
 * order. This ensures that despite different processing latencies on each path,
 * downstream components receive frames in the proper sequence.
 *
 * When bypass routing is disabled, all frames come from the inference path. The
 * merger still operates to enforce ordering and apply buffering rules.
 */
struct frame_merger {
    /**
     * @brief Constructs a frame merger with the given configuration.
     * @param config Configuration parameters for buffer sizing and timeouts.
     */
    explicit frame_merger(frame_merger_config config);

    /**
     * @brief Adds a frame from the inference appsink.
     * @param sample The GstSample pulled from appsink_inference.
     */
    void add_inference_frame(sf::unique_gst_sample_ptr sample);

    /**
     * @brief Adds a frame from the bypass appsink.
     * @param sample The GstSample pulled from appsink_bypass.
     * @note Only called when bypass routing is enabled.
     */
    void add_bypass_frame(sf::unique_gst_sample_ptr sample);

    /**
     * @brief Gets the next frame in PTS order.
     *
     * Blocks until a frame is available or the merger is closed.
     * Returns std::nullopt when the merger is closed and all frames are drained.
     *
     * @return The next frame in PTS order, or nullopt if closed.
     */
    [[nodiscard]] std::optional<merged_frame> get_next_frame();

    /**
     * @brief Signals that no more frames will be added.
     *
     * Call this when both appsinks receive EOS. Wakes up any threads
     * blocked on get_next_frame() so they can drain remaining frames.
     */
    void close();

    /**
     * @brief Requests the consumer to stop waiting and drain or exit.
     *
     * Used for application shutdown when the merger is not yet `close()`d: unblocks `get_next_frame()`
     * so a GstTask or thread can join without waiting indefinitely on the condition variable.
     */
    void request_consumer_shutdown();

    /**
     * @brief Returns current statistics.
     * @return A snapshot of frame merger statistics.
     */
    [[nodiscard]] frame_merger_statistics get_statistics() const;

    /**
     * @brief Checks if the merger has been closed.
     * @return True if close() has been called.
     */
    [[nodiscard]] bool is_closed() const;

private:
    void add_frame_internal(sf::unique_gst_sample_ptr sample, frame_path_type path);
    [[nodiscard]] bool can_output_frame_locked() const;
    [[nodiscard]] merged_frame release_oldest_frame_locked();
    void handle_buffer_overflow_locked();
    [[nodiscard]] bool has_frames_from_both_paths_locked() const;

    frame_merger_config config;
    std::map<GstClockTime, merged_frame> reorder_buffer;
    GstClockTime last_output_pts = 0;
    std::chrono::steady_clock::time_point oldest_frame_arrival_time;
    bool has_seen_inference_frame = false;
    bool has_seen_bypass_frame = false;

    mutable std::mutex buffer_mutex;
    std::condition_variable frame_available_cv;
    bool closed = false;
    bool consumer_shutdown_requested = false;

    mutable std::mutex stats_mutex;
    mutable frame_merger_statistics stats;
};
