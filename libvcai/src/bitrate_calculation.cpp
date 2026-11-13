// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../include/bitrate_calculation.h"
#include <gio/gio.h>
#include <gst/gst.h>
#include <iomanip>
#include <sstream>
#include "../include/logging.h"

// Debug category for bitrate calculation utilities.
GST_DEBUG_CATEGORY_STATIC(bitrate_calculation);

void init_bitrate_calculation_debug_category() {
    GST_DEBUG_CATEGORY_INIT(bitrate_calculation, "bitrate-calculation", 0, "Bitrate calculation utilities");
}

namespace sf {
    /**
     * @brief Calculates the average bitrate in kbps.
     * @param file_size_bytes The size of the file in bytes.
     * @param duration_ns The duration of the video in nanoseconds.
     * @return The average bitrate in kbps.
     */
    [[nodiscard]] double calculate_average_bitrate(const gint64 file_size_bytes, const guint64 duration_ns) {
        if (duration_ns == 0) {
            return 0.0;
        }
        return static_cast<double>(file_size_bytes) * 8 * GST_SECOND / static_cast<double>(duration_ns) / 1000.0;
    }

    /**
     * @brief Calculates and prints the average bitrate of the encoded file.
     * @param num_input_frames The number of input frames processed.
     * @param framerate The framerate of the video.
     * @param h265_filesink The GStreamer filesink element containing the output file location.
     * @note h265_filesink is a non-owning pointer - its lifetime is managed by the GStreamer pipeline.
     */
    void calculate_and_print_bitrate(const guint64 num_input_frames, const int framerate, GstElement *h265_filesink) {
        if (num_input_frames > 0 && h265_filesink != nullptr && G_IS_OBJECT(h265_filesink)) {
            const guint64 duration = gst_util_uint64_scale_int(num_input_frames, GST_SECOND, framerate);
            gchar *location = nullptr;
            g_object_get(h265_filesink, "location", &location, nullptr);

            if (location) {
                GFile *file = g_file_new_for_path(location);
                GFileInfo *file_info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                                         G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr);
                if (file_info) {
                    const gint64 file_size = g_file_info_get_size(file_info);
                    const double bitrate_kbps = calculate_average_bitrate(file_size, duration);
                    info(bitrate_calculation, "Average bitrate: %.2f kbps", bitrate_kbps);

                    // Log a structured summary with all raw values for script extraction.
                    // Format is intentionally machine-parseable for BD-rate evaluation scripts.
                    const double duration_s = static_cast<double>(duration) / static_cast<double>(GST_SECOND);
                    const gint64 bitrate_bps =
                            duration_s > 0.0 ? static_cast<gint64>(static_cast<double>(file_size) * 8.0 / duration_s)
                                             : 0;
                    info(bitrate_calculation,
                         "Bitrate summary: file_size_bytes=%" G_GINT64_FORMAT " num_frames=%" G_GUINT64_FORMAT
                         " framerate=%d"
                         " duration_s=%.3f"
                         " bitrate_bps=%" G_GINT64_FORMAT " bitrate_kbps=%.2f"
                         " file=%s",
                         file_size, num_input_frames, framerate, duration_s, bitrate_bps, bitrate_kbps, location);

                    g_object_unref(file_info);
                }
                g_object_unref(file);
                g_free(location);
            }
        }
    }

    bitrate_tracker::bitrate_tracker(const guint64 window_duration_ns) :
        window_duration_ns(window_duration_ns), total_bytes_in_window(0) {}

    void bitrate_tracker::add_sample(const guint64 bytes, const guint64 timestamp_ns) {
        const std::lock_guard lock(mutex);
        const guint64 window_start = timestamp_ns > window_duration_ns ? timestamp_ns - window_duration_ns : 0;

        // Add new sample
        samples.push_back({bytes, timestamp_ns});
        total_bytes_in_window += bytes;

        // Remove samples outside the window
        while (!samples.empty() && samples.front().timestamp_ns < window_start) {
            total_bytes_in_window -= samples.front().bytes;
            samples.pop_front();
        }
    }

    void bitrate_tracker::add_buffer_sample(GstBuffer *buffer, const guint64 bytes) {
        const GstClockTime pts = GST_BUFFER_PTS(buffer);
        const guint64 timestamp_ns = (pts != GST_CLOCK_TIME_NONE) ? pts : gst_clock_get_time(gst_system_clock_obtain());
        add_sample(bytes, timestamp_ns);
    }

    double bitrate_tracker::get_current_bitrate_kbps() const {
        const std::lock_guard lock(mutex);

        if (samples.empty()) {
            return 0.0;
        }

        const guint64 oldest_timestamp = samples.front().timestamp_ns;
        const guint64 newest_timestamp = samples.back().timestamp_ns;

        if (newest_timestamp <= oldest_timestamp) {
            // All samples have the same timestamp or invalid data
            return 0.0;
        }

        const guint64 duration_ns = newest_timestamp - oldest_timestamp;
        if (duration_ns == 0) {
            return 0.0;
        }

        return sf::calculate_average_bitrate(static_cast<gint64>(total_bytes_in_window), duration_ns);
    }

    std::string bitrate_tracker::format_bitrate_string() const {
        const double bitrate_kbps = get_current_bitrate_kbps();
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);

        // Use Mbps for bitrates >= 1000 kbps, otherwise use kbps
        if (bitrate_kbps >= 1000.0) {
            const double bitrate_mbps = bitrate_kbps / 1000.0;
            oss << bitrate_mbps << " Mbps";
        } else {
            oss << bitrate_kbps << " kbps";
        }

        return oss.str();
    }
} // namespace sf
