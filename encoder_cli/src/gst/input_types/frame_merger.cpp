// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "frame_merger.h"

#include "../../../../libvcai/include/logging.h"

#include <gst/gst.h>

/**
 * @file frame_merger.cpp
 * @brief Implementation of the frame merger for dual-path bypass mode.
 */

/// GStreamer debug category for frame merger diagnostics.
GST_DEBUG_CATEGORY_STATIC(frame_merger_cat);
#define GST_CAT_DEFAULT frame_merger_cat

void init_frame_merger_debug_category() {
    GST_DEBUG_CATEGORY_INIT(frame_merger_cat, "frame-merger", 0, "Frame merger for dual appsink bypass");
}

frame_merger::frame_merger(frame_merger_config merger_config) :
    config(merger_config), reorder_buffer(), last_output_pts(0), oldest_frame_arrival_time(),
    has_seen_inference_frame(false), has_seen_bypass_frame(false), buffer_mutex(), frame_available_cv(), closed(false),
    stats_mutex(), stats() {
    init_frame_merger_debug_category();
    sf::info(frame_merger_cat,
             "Frame merger initialized: max_buffer_frames=%zu, max_latency_ms=%u, emergency_threshold=%zu",
             config.max_buffer_frames, config.max_latency_ms, config.emergency_drop_threshold);
}

void frame_merger::add_inference_frame(sf::unique_gst_sample_ptr sample) {
    add_frame_internal(std::move(sample), frame_path_type::inference);
}

void frame_merger::add_bypass_frame(sf::unique_gst_sample_ptr sample) {
    add_frame_internal(std::move(sample), frame_path_type::bypass);
}

/**
 * @brief Inserts a frame into the reorder buffer with path metadata.
 * @param sample Sample to enqueue.
 * @param path Path metadata for the frame.
 */
void frame_merger::add_frame_internal(sf::unique_gst_sample_ptr sample, frame_path_type path) {
    auto *buffer = gst_sample_get_buffer(sample.get());
    if (!buffer) {
        sf::error(frame_merger_cat, "Sample has no buffer, dropping frame");
        return;
    }

    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    if (pts == GST_CLOCK_TIME_NONE) {
        sf::warning(frame_merger_cat, "Frame has no PTS, using last_output_pts + 1");
        // Use a synthetic PTS based on last output
        // This shouldn't happen in normal operation, but handle gracefully
    }

    {
        std::lock_guard lock(buffer_mutex);

        // Update statistics and track which paths we've seen
        {
            std::lock_guard stats_lock(stats_mutex);
            if (path == frame_path_type::inference) {
                stats.frames_from_inference++;
                has_seen_inference_frame = true;
            } else {
                stats.frames_from_bypass++;
                has_seen_bypass_frame = true;
            }
        }

        // Record arrival time for timeout calculation (if first frame in buffer)
        if (reorder_buffer.empty()) {
            oldest_frame_arrival_time = std::chrono::steady_clock::now();
        }

        // Insert into reorder buffer
        merged_frame frame{};
        frame.sample = std::move(sample);
        frame.path = path;
        frame.pts =
                (pts != GST_CLOCK_TIME_NONE) ? pts : (last_output_pts + GST_SECOND / 30); // Default to 30fps if no PTS

        reorder_buffer[frame.pts] = std::move(frame);

        // Update peak buffer size
        {
            std::lock_guard stats_lock(stats_mutex);
            if (reorder_buffer.size() > stats.peak_buffer_size) {
                stats.peak_buffer_size = reorder_buffer.size();
            }
        }

        // Check for buffer overflow
        if (reorder_buffer.size() >= config.emergency_drop_threshold) {
            handle_buffer_overflow_locked();
        }

        // Notify waiting consumer
        frame_available_cv.notify_one();
    }
}

/**
 * @brief Returns the next available frame in PTS order.
 * @return Next merged frame or nullopt when drained.
 */
std::optional<merged_frame> frame_merger::get_next_frame() {
    std::unique_lock lock(buffer_mutex);

    frame_available_cv.wait(lock, [this] {
        return can_output_frame_locked() || (closed && reorder_buffer.empty()) ||
               (consumer_shutdown_requested && reorder_buffer.empty());
    });

    if (reorder_buffer.empty()) {
        return std::nullopt;
    }

    return release_oldest_frame_locked();
}

/**
 * @brief Determines whether the oldest frame can be output.
 * @return True if output criteria are met.
 */
bool frame_merger::can_output_frame_locked() const {
    if (reorder_buffer.empty()) {
        return false;
    }

    if (consumer_shutdown_requested) {
        return true;
    }

    if (closed) {
        return true;
    }

    // Check if we have frames from both paths - if so, we can output immediately
    // This ensures we don't output bypass frames before inference frames arrive
    if (has_frames_from_both_paths_locked()) {
        return true;
    }

    // If we only have frames from one path, wait for timeout to allow the other path to catch up
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_frame_arrival_time);

    if (elapsed.count() >= static_cast<long>(config.max_latency_ms)) {
        std::lock_guard stats_lock(stats_mutex);
        stats.timeout_events++;
        return true;
    }

    return false;
}

/**
 * @brief Checks whether frames from both paths are buffered.
 */
bool frame_merger::has_frames_from_both_paths_locked() const {
    if (reorder_buffer.empty()) {
        return false;
    }

    // Check if we have frames from both paths
    bool has_inference = false;
    bool has_bypass = false;
    for (const auto &[pts, frame]: reorder_buffer) {
        if (frame.path == frame_path_type::inference) {
            has_inference = true;
        } else {
            has_bypass = true;
        }
        if (has_inference && has_bypass) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Removes and returns the oldest frame from the buffer.
 */
merged_frame frame_merger::release_oldest_frame_locked() {
    if (reorder_buffer.empty()) {
        sf::error(frame_merger_cat, "Attempted to release frame from empty buffer");
        merged_frame empty{};
        empty.pts = GST_CLOCK_TIME_NONE;
        return empty;
    }

    // Get oldest frame (lowest PTS)
    auto it = reorder_buffer.begin();
    auto frame = std::move(it->second);
    const GstClockTime frame_pts = frame.pts;

    // Check for reorder event
    if (last_output_pts != 0 && frame_pts < last_output_pts) {
        std::lock_guard stats_lock(stats_mutex);
        stats.reorder_events++;
        sf::warning(frame_merger_cat,
                    "Frame reorder detected: output PTS=%" GST_TIME_FORMAT " < last_output_pts=%" GST_TIME_FORMAT,
                    GST_TIME_ARGS(frame_pts), GST_TIME_ARGS(last_output_pts));
    }

    last_output_pts = frame_pts;
    reorder_buffer.erase(it);

    // Update oldest_frame_arrival_time if buffer not empty
    if (!reorder_buffer.empty()) {
        oldest_frame_arrival_time = std::chrono::steady_clock::now();
    }

    // Update statistics
    {
        std::lock_guard stats_lock(stats_mutex);
        stats.frames_output++;
    }

    return frame;
}

/**
 * @brief Handles buffer overflow by forcing output of the oldest frame.
 */
void frame_merger::handle_buffer_overflow_locked() {
    sf::warning(frame_merger_cat, "Reorder buffer overflow (%zu frames >= %zu threshold), forcing oldest frame output",
                reorder_buffer.size(), config.emergency_drop_threshold);
    // The frame will be released by the caller after this function returns
    // This function just logs the warning
}

void frame_merger::close() {
    {
        std::lock_guard lock(buffer_mutex);
        if (closed) {
            return; // Already closed
        }
        closed = true;
        sf::info(frame_merger_cat, "Frame merger closed, draining %zu remaining frames", reorder_buffer.size());
    }
    frame_available_cv.notify_all();
}

void frame_merger::request_consumer_shutdown() {
    {
        std::lock_guard lock(buffer_mutex);
        if (consumer_shutdown_requested) {
            return;
        }
        consumer_shutdown_requested = true;
        sf::debug(frame_merger_cat, "Frame merger consumer shutdown requested");
    }
    frame_available_cv.notify_all();
}

/**
 * @brief Returns a snapshot of merger statistics.
 */
frame_merger_statistics frame_merger::get_statistics() const {
    std::lock_guard lock(stats_mutex);
    return stats;
}

/**
 * @brief Indicates whether the merger has been closed.
 */
bool frame_merger::is_closed() const {
    std::lock_guard lock(buffer_mutex);
    return closed;
}
