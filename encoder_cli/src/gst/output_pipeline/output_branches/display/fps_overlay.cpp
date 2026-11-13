// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "fps_overlay.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <iomanip>
#include <sstream>
#include "../../../../../libvcai/include/logging.h"
#include "../../../../../libvcai/include/text_drawer.h"

/**
 * @file fps_overlay.cpp
 * @brief Implementation of FPS overlay functionality.
 */

/// GStreamer debug category for FPS overlay rendering.
GST_DEBUG_CATEGORY_STATIC(fps_overlay);

namespace {

    /**
     * @brief Formats FPS value as a string.
     *
     * @param fps The FPS value to format.
     * @return Formatted string (e.g., "30.0 fps").
     */
    [[nodiscard]] std::string format_fps_string(const double fps) {
        if (fps <= 0.0) {
            return "";
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << fps << " fps";
        return oss.str();
    }

    /**
     * @brief Pad probe callback to measure FPS and draw overlay on frames.
     */
    GstPadProbeReturn fps_overlay_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        auto *data = static_cast<global_data *>(user_data);

        if (!data->params.debug.draw_fps) {
            return GST_PAD_PROBE_OK;
        }

        if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
            const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
            if (!buffer) {
                return GST_PAD_PROBE_OK;
            }

            // Get current FPS from pipeline FPS monitor
            const double fps = data->monitoring.fps_monitor.get_current_fps("display");
            if (fps <= 0.0) {
                return GST_PAD_PROBE_OK; // Not enough samples yet or pipeline inactive
            }

            const std::string fps_str = format_fps_string(fps);

            // Get frame dimensions from caps
            if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
                GstVideoInfo video_info;
                if (gst_video_info_from_caps(&video_info, caps)) {
                    // Make buffer writable if needed
                    GstBuffer *writable_buffer = buffer;
                    if (!gst_buffer_is_writable(buffer)) {
                        writable_buffer = gst_buffer_make_writable(buffer);
                        if (!writable_buffer) {
                            gst_caps_unref(caps);
                            return GST_PAD_PROBE_OK;
                        }
                        GST_PAD_PROBE_INFO_DATA(info) = writable_buffer;
                    }

                    // Draw FPS on frame based on format
                    const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&video_info);
                    // Calculate position: top right corner with padding
                    constexpr int padding_x = 10;
                    constexpr int padding_y = 10;
                    const int width = GST_VIDEO_INFO_WIDTH(&video_info);
                    constexpr int char_width = 8;
                    constexpr int char_spacing = 1;
                    // Calculate text width (approximate)
                    const int text_width = static_cast<int>(fps_str.length()) * (char_width + char_spacing);
                    const int start_x = width - text_width - padding_x;
                    constexpr int start_y = padding_y;

                    if (format == GST_VIDEO_FORMAT_NV12) {
                        if (!sf::draw_text_on_nv12_frame(writable_buffer, &video_info, fps_str, start_x, start_y)) {
                            sf::warning(fps_overlay, "Failed to draw FPS on NV12 frame");
                        }
                    } else if (format == GST_VIDEO_FORMAT_I420) {
                        if (!sf::draw_text_on_i420_frame(writable_buffer, &video_info, fps_str, start_x, start_y)) {
                            sf::warning(fps_overlay, "Failed to draw FPS on I420 frame");
                        }
                    } else {
                        sf::warning(fps_overlay, "FPS overlay only supports NV12 and I420 formats, got format %d",
                                    format);
                    }
                }
                gst_caps_unref(caps);
            }
        }

        return GST_PAD_PROBE_OK;
    }
} // namespace

/**
 * @brief Initializes the FPS overlay debug category.
 */
void init_fps_overlay_debug_category() {
    GST_DEBUG_CATEGORY_INIT(fps_overlay, "fps-overlay", 0, "FPS overlay for display branch");
}

/**
 * @brief Attaches an FPS overlay probe to the display converter.
 * @param converter Converter element to attach the probe to.
 * @param data Global data for configuration and FPS source.
 * @return True if the probe was installed.
 */
bool setup_fps_overlay(GstElement *converter, const global_data &data) {
    if (!data.params.debug.draw_fps) {
        return true; // Not an error, just disabled
    }

    if (!converter) {
        sf::error(fps_overlay, "Cannot setup FPS overlay: converter element is null");
        return false;
    }

    GstPad *const src_pad = gst_element_get_static_pad(converter, "src");
    if (!src_pad) {
        sf::error(fps_overlay, "Failed to get src pad from converter");
        return false;
    }

    auto *user_data = const_cast<global_data *>(&data);
    const gulong probe_id =
            gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, fps_overlay_probe, user_data, nullptr);
    if (probe_id == 0) {
        sf::error(fps_overlay, "Failed to add pad probe to converter for FPS overlay");
        gst_object_unref(src_pad);
        return false;
    }

    sf::info(fps_overlay, "FPS overlay enabled on converter output (using pipeline FPS monitor)");
    gst_object_unref(src_pad);
    return true;
}

/**
 * @brief Clears FPS tracking state (no-op with pipeline FPS monitor).
 */
void clear_fps_tracker() {
    // FPS tracking is now handled by pipeline_fps_monitor in global_data
    // This function is kept for API compatibility but does nothing
    sf::debug(fps_overlay, "clear_fps_tracker called (FPS tracking now handled by pipeline FPS monitor)");
}
