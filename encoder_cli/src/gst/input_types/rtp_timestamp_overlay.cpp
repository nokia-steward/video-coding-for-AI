// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rtp_timestamp_overlay.h"
#include "../../processing/frame_processing_utils.h"
#include "gst_raii.h"

#include <algorithm>
#include <gst/gst.h>
#include <gst/gstminiobject.h>
#include <gst/video/video.h>
#include <iomanip>
#include <sstream>
#include "../../../libvcai/include/logging.h"
#include "../../../libvcai/include/rectangle.h"
#include "../../../libvcai/include/text_drawer.h"

/**
 * @file rtp_timestamp_overlay.cpp
 * @brief Implementation of RTP timestamp extraction and overlay functionality.
 */

/// GStreamer debug category for RTP timestamp overlay.
GST_DEBUG_CATEGORY_STATIC(rtp_timestamp_overlay);

namespace {
    /// Protects access to the RTP timestamp map.
    std::mutex rtp_timestamp_map_mutex;
    /// Stores RTP timestamps keyed by buffer PTS.
    std::map<GstClockTime, guint32> rtp_timestamp_map;
    /// RTP clock rate used for timestamp conversion.
    guint32 rtp_clock_rate = 90000;
    /// First RTP timestamp used to compute relative offsets.
    guint32 first_rtp_timestamp = 0;

    /// GQuark identifier for storing RTP timestamp in buffer qdata.
    GQuark rtp_timestamp_quark = 0;

    /**
     * @brief Initializes the GQuark for RTP timestamp storage.
     */
    void init_rtp_timestamp_quark() {
        if (rtp_timestamp_quark == 0) {
            rtp_timestamp_quark = g_quark_from_static_string("rtp-timestamp");
        }
    }

    /**
     * @brief Attaches RTP timestamp to buffer using qdata.
     *
     * @param buffer The buffer to attach timestamp to.
     * @param rtp_timestamp The RTP timestamp value.
     */
    void attach_rtp_timestamp_to_buffer(GstBuffer *buffer, const guint32 rtp_timestamp) {
        if (!buffer) {
            return;
        }
        init_rtp_timestamp_quark();
        auto *timestamp_ptr = static_cast<guint32 *>(g_malloc(sizeof(guint32)));
        *timestamp_ptr = rtp_timestamp;
        // GstBuffer is a GstMiniObject, so use gst_mini_object_set_qdata
        gst_mini_object_set_qdata(GST_MINI_OBJECT(buffer), rtp_timestamp_quark, timestamp_ptr,
                                  [](gpointer data) { g_free(data); });
    }

    /**
     * @brief Extracts RTP timestamp from buffer qdata.
     *
     * @param buffer The buffer to extract timestamp from.
     * @return RTP timestamp if found, or 0 if not found.
     */
    [[nodiscard]] guint32 get_rtp_timestamp_from_buffer(GstBuffer *buffer) {
        if (!buffer) {
            return 0;
        }
        init_rtp_timestamp_quark();
        const guint32 *timestamp_ptr =
                static_cast<guint32 *>(gst_mini_object_get_qdata(GST_MINI_OBJECT(buffer), rtp_timestamp_quark));
        if (timestamp_ptr) {
            return *timestamp_ptr;
        }
        return 0;
    }

    /**
     * @brief Converts RTP timestamp to human-readable format (HH:MM:SS.mmm).
     *
     * @param rtp_timestamp The RTP timestamp value.
     * @param clock_rate The RTP clock rate (typically 90000).
     * @return Formatted timestamp string.
     */
    [[nodiscard]] std::string format_rtp_timestamp(const guint32 rtp_timestamp, const guint32 clock_rate) {
        // Calculate time offset from first timestamp
        guint32 timestamp_offset = 0;
        if (first_rtp_timestamp == 0) {
            // First timestamp - use as reference point
            first_rtp_timestamp = rtp_timestamp;
            timestamp_offset = 0;
        } else {
            // Handle wraparound (RTP timestamp is 32-bit)
            // Check if wraparound occurred (current timestamp is less than first)
            if (rtp_timestamp < first_rtp_timestamp) {
                // Wraparound occurred: calculate offset accounting for 32-bit wraparound
                constexpr guint32 max_timestamp = 0xFFFFFFFFU;
                timestamp_offset = (max_timestamp - first_rtp_timestamp) + rtp_timestamp + 1U;
            } else {
                // Normal case: simple subtraction
                timestamp_offset = rtp_timestamp - first_rtp_timestamp;
            }
        }

        // Convert to seconds (double precision for accuracy)
        const double seconds = static_cast<double>(timestamp_offset) / static_cast<double>(clock_rate);

        // Extract hours, minutes, seconds, and milliseconds
        const auto total_seconds = static_cast<int64_t>(seconds);
        const auto hours = total_seconds / 3600;
        const auto minutes = (total_seconds % 3600) / 60;
        const auto secs = total_seconds % 60;
        const auto milliseconds = static_cast<int>((seconds - static_cast<double>(total_seconds)) * 1000.0);

        // Format as HH:MM:SS.mmm
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << hours << ":" << std::setw(2) << minutes << ":" << std::setw(2)
            << secs << "." << std::setw(3) << milliseconds;

        return oss.str();
    }


    /**
     * @brief Parses RTP header from buffer data to extract timestamp.
     *
     * RTP header format (RFC 3550):
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |V=2|P|X|  CC   |M|     PT      |       sequence number         |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |                           timestamp                           |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |           synchronization source (SSRC) identifier            |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *
     * @param buffer_data Pointer to buffer data containing RTP packet.
     * @param buffer_size Size of buffer data.
     * @return RTP timestamp if successfully parsed, or 0 if failed.
     */
    [[nodiscard]] guint32 parse_rtp_timestamp_from_buffer(const uint8_t *buffer_data, const gsize buffer_size) {
        if (!buffer_data || buffer_size < 12) {
            // RTP header is at least 12 bytes
            return 0;
        }

        // Check RTP version (bits 0-1 of first byte should be 2)
        if ((buffer_data[0] & 0xC0) != 0x80) {
            // Not a valid RTP packet (version should be 2)
            return 0;
        }

        // Extract timestamp from bytes 4-7 (big-endian)
        const guint32 rtp_timestamp =
                (static_cast<guint32>(buffer_data[4]) << 24) | (static_cast<guint32>(buffer_data[5]) << 16) |
                (static_cast<guint32>(buffer_data[6]) << 8) | static_cast<guint32>(buffer_data[7]);

        return rtp_timestamp;
    }

    GstPadProbeReturn rtp_timestamp_extraction_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                                                     [[maybe_unused]] gpointer user_data) {
        if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
            const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
            const GstClockTime pts = GST_BUFFER_PTS(buffer);

            if (pts != GST_CLOCK_TIME_NONE && pts < GST_CLOCK_TIME_NONE / 2) {
                // Extract RTP timestamp from buffer data (RTP header)
                GstMapInfo map_info;
                if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
                    const guint32 rtp_timestamp = parse_rtp_timestamp_from_buffer(map_info.data, map_info.size);
                    if (rtp_timestamp != 0) {
                        // Store RTP timestamp keyed by PTS
                        // Note: Multiple RTP packets may have the same PTS (they're part of the same frame)
                        // We'll use the last one we see for each PTS
                        std::lock_guard lock(rtp_timestamp_map_mutex);
                        rtp_timestamp_map[pts] = rtp_timestamp;
                    }
                    gst_buffer_unmap(buffer, &map_info);
                }
            }
        }

        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Finds the closest RTP timestamp for a given PTS.
     *
     * Uses the ordered nature of std::map for O(log n) lookup.
     * Since PTS values might not match exactly due to timestamp conversion,
     * we find the closest match within a reasonable tolerance.
     * Also prunes entries older than 30 seconds to prevent unbounded growth.
     *
     * @param pts The PTS to look up.
     * @return RTP timestamp if found, or 0 if not found.
     */
    [[nodiscard]] guint32 find_closest_rtp_timestamp(const GstClockTime pts) {
        std::lock_guard lock(rtp_timestamp_map_mutex);
        if (rtp_timestamp_map.empty()) {
            return 0;
        }

        // Try exact match first
        const auto exact_it = rtp_timestamp_map.find(pts);
        if (exact_it != rtp_timestamp_map.end()) {
            return exact_it->second;
        }

        // Use lower_bound for O(log n) lookup on ordered map
        // Find the first entry >= pts, then check it and its predecessor
        constexpr GstClockTime tolerance = 5 * GST_SECOND; // Increased tolerance for random_access buffering
        guint32 closest_rtp = 0;
        GstClockTime min_diff = GST_CLOCK_TIME_NONE;

        const auto it = rtp_timestamp_map.lower_bound(pts);

        // Check the entry at or just after pts
        if (it != rtp_timestamp_map.end()) {
            const GstClockTime diff = it->first - pts;
            if (diff < tolerance && diff < min_diff) {
                min_diff = diff;
                closest_rtp = it->second;
            }
        }

        // Check the entry just before pts
        if (it != rtp_timestamp_map.begin()) {
            const auto prev_it = std::prev(it);
            const GstClockTime diff = pts - prev_it->first;
            if (diff < tolerance && diff < min_diff) {
                min_diff = diff;
                closest_rtp = prev_it->second;
            }
        }

        // Prune old entries (older than 30 seconds before current pts) to prevent unbounded growth
        constexpr GstClockTime prune_threshold = 30 * GST_SECOND;
        if (pts > prune_threshold) {
            const GstClockTime prune_before = pts - prune_threshold;
            const auto prune_it = rtp_timestamp_map.lower_bound(prune_before);
            if (prune_it != rtp_timestamp_map.begin()) {
                rtp_timestamp_map.erase(rtp_timestamp_map.begin(), prune_it);
            }
        }

        return closest_rtp;
    }

    /**
     * @brief Pad probe callback to attach RTP timestamp to decoded frame buffers.
     */
    GstPadProbeReturn rtp_timestamp_attachment_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                                                     [[maybe_unused]] gpointer user_data) {
        if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
            const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
            const GstClockTime pts = GST_BUFFER_PTS(buffer);

            if (pts != GST_CLOCK_TIME_NONE && pts < GST_CLOCK_TIME_NONE / 2) {
                // Look up RTP timestamp for this PTS (with tolerance for timestamp conversion)
                const guint32 rtp_timestamp = find_closest_rtp_timestamp(pts);
                if (rtp_timestamp != 0) {
                    // Attach RTP timestamp to buffer
                    attach_rtp_timestamp_to_buffer(buffer, rtp_timestamp);
                }
            }
        }

        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to draw RTP timestamp on frames before encoding.
     */
    GstPadProbeReturn rtp_timestamp_overlay_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        const auto *config = static_cast<const encoder_config *>(user_data);

        if (!config->debug.draw_rtp_timestamp) {
            return GST_PAD_PROBE_OK;
        }

        if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
            // Make buffer writable before modification
            GstBuffer *buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));
            GST_PAD_PROBE_INFO_DATA(info) = buffer;

            const GstClockTime pts = GST_BUFFER_PTS(buffer);

            if (pts != GST_CLOCK_TIME_NONE && pts < GST_CLOCK_TIME_NONE / 2) {
                // Try to get RTP timestamp from buffer metadata first
                guint32 rtp_timestamp = get_rtp_timestamp_from_buffer(buffer);

                // If not found in metadata, look it up by PTS (buffers may have been copied)
                if (rtp_timestamp == 0) {
                    rtp_timestamp = find_closest_rtp_timestamp(pts);
                }

                if (rtp_timestamp != 0) {
                    const std::string timestamp_str = format_rtp_timestamp(rtp_timestamp, rtp_clock_rate);

                    // Get frame dimensions from caps
                    if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
                        GstVideoInfo video_info;
                        if (gst_video_info_from_caps(&video_info, caps)) {
                            // Draw timestamp on frame based on format
                            const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&video_info);
                            // Calculate position: lower left corner with padding
                            constexpr int padding_x = 10;
                            constexpr int padding_y = 10;
                            const int height = GST_VIDEO_INFO_HEIGHT(&video_info);
                            constexpr int start_x = padding_x;
                            const int start_y = height - sf::TEXT_CHAR_HEIGHT - padding_y;

                            if (format == GST_VIDEO_FORMAT_NV12) {
                                if (!sf::draw_text_on_nv12_frame(buffer, &video_info, timestamp_str, start_x,
                                                                 start_y)) {
                                    sf::warning(rtp_timestamp_overlay, "Failed to draw timestamp on NV12 frame");
                                }
                            } else if (format == GST_VIDEO_FORMAT_I420) {
                                if (!sf::draw_text_on_i420_frame(buffer, &video_info, timestamp_str, start_x,
                                                                 start_y)) {
                                    sf::warning(rtp_timestamp_overlay, "Failed to draw timestamp on I420 frame");
                                }
                            } else {
                                sf::warning(rtp_timestamp_overlay,
                                            "RTP timestamp overlay only supports NV12 and I420 formats, got format %d",
                                            format);
                            }
                        }
                        gst_caps_unref(caps);
                    }
                }
            }
        }

        return GST_PAD_PROBE_OK;
    }
} // namespace

void init_rtp_timestamp_overlay_debug_category() {
    GST_DEBUG_CATEGORY_INIT(rtp_timestamp_overlay, "rtp-timestamp-overlay", 0, "RTP timestamp extraction and overlay");
}

/**
 * @brief Installs a probe to extract RTP timestamps from the jitterbuffer.
 * @param jitterbuffer Jitterbuffer element to attach the probe to.
 * @param clock_rate RTP clock rate for timestamp conversion.
 * @return True if the probe was installed.
 */
bool setup_rtp_timestamp_extraction(GstElement *jitterbuffer, const guint32 clock_rate) {
    if (!jitterbuffer) {
        sf::error(rtp_timestamp_overlay, "Cannot setup RTP timestamp extraction: jitterbuffer element is null");
        return false;
    }

    rtp_clock_rate = clock_rate;

    // Get the src pad from jitterbuffer (where RTP caps are still available)
    GstPad *const src_pad = gst_element_get_static_pad(jitterbuffer, "src");
    if (!src_pad) {
        sf::error(rtp_timestamp_overlay, "Failed to get src pad from RTP jitterbuffer");
        return false;
    }

    const gulong probe_id =
            gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, rtp_timestamp_extraction_probe, nullptr, nullptr);
    if (probe_id == 0) {
        sf::error(rtp_timestamp_overlay, "Failed to add pad probe to RTP jitterbuffer for timestamp extraction");
        gst_object_unref(src_pad);
        return false;
    }

    sf::info(rtp_timestamp_overlay, "RTP timestamp extraction enabled on jitterbuffer output (clock rate: %u)",
             clock_rate);
    gst_object_unref(src_pad);
    return true;
}

/**
 * @brief Installs a probe to attach RTP timestamps to decoded buffers.
 * @param decoder Decoder element to attach the probe to.
 * @return True if the probe was installed.
 */
bool setup_rtp_timestamp_attachment(GstElement *decoder) {
    if (!decoder) {
        sf::error(rtp_timestamp_overlay, "Cannot setup RTP timestamp attachment: decoder element is null");
        return false;
    }

    init_rtp_timestamp_quark();

    GstPad *const src_pad = gst_element_get_static_pad(decoder, "src");
    if (!src_pad) {
        sf::error(rtp_timestamp_overlay, "Failed to get src pad from decoder");
        return false;
    }

    const gulong probe_id =
            gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, rtp_timestamp_attachment_probe, nullptr, nullptr);
    if (probe_id == 0) {
        sf::error(rtp_timestamp_overlay, "Failed to add pad probe to decoder for timestamp attachment");
        gst_object_unref(src_pad);
        return false;
    }

    sf::info(rtp_timestamp_overlay, "RTP timestamp attachment enabled on decoder output");
    gst_object_unref(src_pad);
    return true;
}

/**
 * @brief Installs a probe to draw RTP timestamp overlays on encoded frames.
 * @param encoder Encoder element to attach the probe to.
 * @param config Encoder config for configuration.
 * @return True if the probe was installed.
 */
bool setup_rtp_timestamp_overlay(GstElement *encoder, const encoder_config &config) {
    if (!config.debug.draw_rtp_timestamp) {
        return true; // Not an error, just disabled
    }

    if (config.pack_rois) {
        sf::info(rtp_timestamp_overlay,
                 "ROI packing enabled: timestamp drawn in processing; probe added as fallback for encoded output");
    }

    if (!encoder) {
        sf::error(rtp_timestamp_overlay, "Cannot setup RTP timestamp overlay: element is null");
        return false;
    }

    init_rtp_timestamp_quark();

    GstPad *const src_pad = gst_element_get_static_pad(encoder, "src");
    if (!src_pad) {
        sf::error(rtp_timestamp_overlay, "Failed to get src pad from appsrc");
        return false;
    }

    auto *user_data = const_cast<encoder_config *>(&config);
    const gulong probe_id =
            gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, rtp_timestamp_overlay_probe, user_data, nullptr);
    if (probe_id == 0) {
        sf::error(rtp_timestamp_overlay, "Failed to add pad probe to appsrc for timestamp overlay");
        gst_object_unref(src_pad);
        return false;
    }

    sf::info(rtp_timestamp_overlay, "RTP timestamp overlay enabled on appsrc output");
    gst_object_unref(src_pad);
    return true;
}

bool draw_rtp_timestamp_on_buffer(GstBuffer *buffer, const frame_properties &props) {
    if (!buffer) {
        sf::warning(rtp_timestamp_overlay, "Cannot draw RTP timestamp: buffer is null");
        return false;
    }

    if (!props.format) {
        sf::warning(rtp_timestamp_overlay, "Cannot draw RTP timestamp: frame format is unavailable");
        return false;
    }

    const GstClockTime pts = GST_BUFFER_PTS(buffer);
    if (pts == GST_CLOCK_TIME_NONE || pts >= GST_CLOCK_TIME_NONE / 2) {
        return false;
    }

    guint32 rtp_timestamp = get_rtp_timestamp_from_buffer(buffer);
    if (rtp_timestamp == 0) {
        rtp_timestamp = find_closest_rtp_timestamp(pts);
    }
    if (rtp_timestamp == 0) {
        return false;
    }

    const std::string timestamp_str = format_rtp_timestamp(rtp_timestamp, rtp_clock_rate);
    const sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, props.format,
                                                           "width", G_TYPE_INT, props.width, "height", G_TYPE_INT,
                                                           props.height, nullptr));
    if (!caps) {
        sf::warning(rtp_timestamp_overlay, "Cannot draw RTP timestamp: failed to create caps");
        return false;
    }

    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, caps.get())) {
        sf::warning(rtp_timestamp_overlay, "Cannot draw RTP timestamp: failed to parse video info");
        return false;
    }

    if (!gst_buffer_is_writable(buffer)) {
        sf::warning(rtp_timestamp_overlay, "Cannot draw RTP timestamp: buffer is not writable");
        return false;
    }

    const sf::rectangle timestamp_roi = create_timestamp_roi_rectangle(props.width, props.height);
    const int start_x = timestamp_roi.left;
    const int start_y = timestamp_roi.top;

    const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&video_info);
    if (format == GST_VIDEO_FORMAT_NV12) {
        if (!sf::draw_text_on_nv12_frame(buffer, &video_info, timestamp_str, start_x, start_y)) {
            sf::warning(rtp_timestamp_overlay, "Failed to draw RTP timestamp on NV12 frame");
            return false;
        }
        return true;
    }

    if (format == GST_VIDEO_FORMAT_I420) {
        if (!sf::draw_text_on_i420_frame(buffer, &video_info, timestamp_str, start_x, start_y)) {
            sf::warning(rtp_timestamp_overlay, "Failed to draw RTP timestamp on I420 frame");
            return false;
        }
        return true;
    }

    sf::warning(rtp_timestamp_overlay, "RTP timestamp overlay only supports NV12 and I420 formats, got format %d",
                format);
    return false;
}

/**
 * @brief Builds a rectangle for the RTP timestamp overlay region.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @return Rectangle for overlay placement.
 */
sf::rectangle create_timestamp_roi_rectangle(const int width, const int height) {
    // Timestamp format is HH:MM:SS.mmm (12 characters)
    // Each character is 8 pixels wide with 1 pixel spacing
    constexpr int padding_x = 10;
    constexpr int padding_y = 10;
    constexpr int max_timestamp_length = 12; // HH:MM:SS.mmm
    constexpr int char_width = 8;
    constexpr int char_height = 12;
    constexpr int char_spacing = 1;

    constexpr int timestamp_width = max_timestamp_length * (char_width + char_spacing) - char_spacing;
    constexpr int timestamp_height = char_height;
    constexpr int start_x = padding_x;
    const int start_y = height - timestamp_height - padding_y;

    // Clamp to frame boundaries
    constexpr int clamped_x = std::max(0, start_x);
    const int clamped_y = std::max(0, start_y);
    const int clamped_w = std::min(timestamp_width, width - clamped_x);
    const int clamped_h = std::min(timestamp_height, height - clamped_y);

    return sf::rectangle(clamped_x, clamped_y, clamped_w, clamped_h);
}

void clear_rtp_timestamps() {
    std::lock_guard lock(rtp_timestamp_map_mutex);
    rtp_timestamp_map.clear();
    first_rtp_timestamp = 0;
    sf::info(rtp_timestamp_overlay, "Cleared RTP timestamp storage");
}
