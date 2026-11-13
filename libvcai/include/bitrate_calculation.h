// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <deque>
#include <gst/gst.h>
#include <mutex>

/**
 * @brief Initializes the debug category for bitrate calculation operations.
 */
void init_bitrate_calculation_debug_category();

namespace sf {
    /**
     * @brief Calculates the average bitrate in kbps.
     * @param file_size_bytes The size of the file in bytes.
     * @param duration_ns The duration of the video in nanoseconds.
     * @return The average bitrate in kbps.
     */
    [[nodiscard]] double calculate_average_bitrate(gint64 file_size_bytes, guint64 duration_ns);

    /**
     * @brief Calculates and prints the average bitrate of the encoded file.
     *
     * Logs "Average bitrate: X.XX kbps" followed by a structured
     * "Bitrate summary: ..." line containing file_size_bytes, num_frames,
     * framerate, duration_s, bitrate_bps, bitrate_kbps, and file path.
     * The structured line is machine-parseable for BD-rate evaluation scripts.
     *
     * @param num_input_frames The number of input frames processed.
     * @param framerate The framerate of the video.
     * @param h265_filesink The GStreamer filesink element containing the output file location.
     */
    void calculate_and_print_bitrate(guint64 num_input_frames, int framerate, GstElement *h265_filesink);
    /**
     * @brief Structure to track bytes and timestamp for bitrate calculation.
     */
    struct bitrate_sample {
        guint64 bytes;
        guint64 timestamp_ns;
    };

    /**
     * @brief Real-time bitrate tracker using a sliding window approach.
     *
     * This class tracks incoming buffer sizes and timestamps over a sliding time window
     * to calculate the current bitrate of a video stream.
     */
    struct bitrate_tracker {
        /**
         * @brief Constructor.
         * @param window_duration_ns Duration of the sliding window in nanoseconds (default: 1 second).
         */
        explicit bitrate_tracker(guint64 window_duration_ns = GST_SECOND);

        /**
         * @brief Adds a buffer sample to the tracker.
         * @param bytes Size of the buffer in bytes.
         * @param timestamp_ns Timestamp of the buffer in nanoseconds.
         */
        void add_sample(guint64 bytes, guint64 timestamp_ns);

        /**
         * @brief Adds a buffer sample using buffer PTS or system clock fallback.
         * @param buffer The GstBuffer to extract timestamp from (non-const due to GStreamer API).
         * @param bytes Size of the buffer in bytes.
         */
        void add_buffer_sample(GstBuffer *buffer, guint64 bytes);

        /**
         * @brief Calculates the current bitrate based on samples in the window.
         * @return Current bitrate in kbps, or 0.0 if insufficient data.
         */
        [[nodiscard]] double get_current_bitrate_kbps() const;

        /**
         * @brief Formats the current bitrate as a string for display.
         * @return Formatted string (e.g., "1234.56 kbps" or "0.00 kbps").
         */
        [[nodiscard]] std::string format_bitrate_string() const;

    private:
        mutable std::mutex mutex;
        std::deque<bitrate_sample> samples;
        guint64 window_duration_ns;
        guint64 total_bytes_in_window;
    };
} // namespace sf
