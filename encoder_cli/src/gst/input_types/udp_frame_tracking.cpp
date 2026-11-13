// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "udp_frame_tracking.h"

#include <gst/gst.h>

#include "../../../libvcai/include/logging.h"
#include "../../global_data.h"
#include "frame_drop_manager.h"
#include "input_ingress_facade.h"
#include "udp_input.h"

/**
 * @file udp_frame_tracking.cpp
 * @brief Frame-tracking pad probe setup for UDP ingress.
 */

/// GStreamer debug category for UDP frame-tracking diagnostics.
GST_DEBUG_CATEGORY_STATIC(udp_frame_tracking);

namespace {
    /// Interval for logging periodic frame statistics.
    constexpr guint64 frame_log_interval = 100;

    /**
     * @brief Pad probe callback to track frames after parser.
     */
    GstPadProbeReturn parser_output_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0 || user_data == nullptr) {
            return GST_PAD_PROBE_OK;
        }
        auto &ur = static_cast<global_data *>(user_data)->udp_input_runtime;
        const guint64 frame_num = ur.frames_after_parser.fetch_add(1) + 1;
        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        GstMapInfo map_info;
        gsize buffer_size = 0;

        if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            buffer_size = map_info.size;
            gst_buffer_unmap(buffer, &map_info);
        }

        if (frame_num % frame_log_interval == 0) {
            sf::debug(udp_frame_tracking, "Frame after parser: #%lu, size: %zu bytes", frame_num, buffer_size);
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to log full decoded frames after decoder and implement adaptive frame dropping.
     */
    GstPadProbeReturn parser_full_frame_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }

        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);

        if (user_data == nullptr) {
            sf::warning(udp_frame_tracking, "parser_full_frame_probe called but user_data is null");
            return GST_PAD_PROBE_OK;
        }

        auto *data = static_cast<global_data *>(user_data);
        const guint64 call_num = data->udp_input_runtime.parser_full_frame_probe_call_count.fetch_add(1) + 1;

        if (call_num == 1) {
            sf::info(udp_frame_tracking, "parser_full_frame_probe first call - frame drop manager will be invoked");
        }

        const input_ingress_facade view(*data);
        if (view.udp_frame_drop_manager.should_drop_frame(buffer, view)) {
            return GST_PAD_PROBE_DROP;
        }

        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames going into queue before decoder.
     */
    GstPadProbeReturn queue_before_decoder_input_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                                                       gpointer user_data) {
        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0 || user_data == nullptr) {
            return GST_PAD_PROBE_OK;
        }
        auto &ur = static_cast<global_data *>(user_data)->udp_input_runtime;
        const guint64 frame_num = ur.frames_into_queue_before_decoder.fetch_add(1) + 1;
        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        GstMapInfo map_info;
        gsize buffer_size = 0;

        if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            buffer_size = map_info.size;
            gst_buffer_unmap(buffer, &map_info);
        }

        if (frame_num % frame_log_interval == 0) {
            sf::debug(udp_frame_tracking, "Frame into queue-before-decoder: #%lu, size: %zu bytes", frame_num,
                      buffer_size);
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames after queue before decoder.
     */
    GstPadProbeReturn queue_before_decoder_output_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                                                        gpointer user_data) {
        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0 || user_data == nullptr) {
            return GST_PAD_PROBE_OK;
        }
        auto &ur = static_cast<global_data *>(user_data)->udp_input_runtime;
        const guint64 frame_num = ur.frames_after_queue_before_decoder.fetch_add(1) + 1;
        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        GstMapInfo map_info;
        gsize buffer_size = 0;

        if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            buffer_size = map_info.size;
            gst_buffer_unmap(buffer, &map_info);
        }

        if (frame_num % frame_log_interval == 0) {
            sf::debug(udp_frame_tracking, "Frame after queue-before-decoder: #%lu, size: %zu bytes", frame_num,
                      buffer_size);
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames going into decoder.
     */
    GstPadProbeReturn decoder_input_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0 || user_data == nullptr) {
            return GST_PAD_PROBE_OK;
        }
        auto &ur = static_cast<global_data *>(user_data)->udp_input_runtime;
        const guint64 frame_num = ur.frames_into_decoder.fetch_add(1) + 1;
        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        GstMapInfo map_info;
        gsize buffer_size = 0;

        if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            buffer_size = map_info.size;
            gst_buffer_unmap(buffer, &map_info);
        }

        if (frame_num % frame_log_interval == 0) {
            sf::debug(udp_frame_tracking, "Frame into decoder: #%lu, size: %zu bytes", frame_num, buffer_size);
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames after decoder.
     */
    GstPadProbeReturn decoder_output_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0 || user_data == nullptr) {
            return GST_PAD_PROBE_OK;
        }
        auto *data = static_cast<global_data *>(user_data);
        auto &ur = data->udp_input_runtime;
        const guint64 frame_num = ur.frames_after_decoder.fetch_add(1) + 1;
        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        GstMapInfo map_info;
        gsize buffer_size = 0;

        if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            buffer_size = map_info.size;
            gst_buffer_unmap(buffer, &map_info);
        }

        if (data->monitoring.status_reporter) {
            data->monitoring.status_reporter->update_udp_decoded();
        }
        if (frame_num % frame_log_interval == 0) {
            sf::debug(udp_frame_tracking, "Frame after decoder: #%lu, size: %zu bytes", frame_num, buffer_size);
        }
        return GST_PAD_PROBE_OK;
    }
} // namespace

void init_udp_frame_tracking_debug_category() {
    GST_DEBUG_CATEGORY_INIT(udp_frame_tracking, "udp-frame-tracking", 0, "UDP ingress frame-tracking probes");
}

bool setup_udp_frame_tracking_probes(const udp_input_elements &elements, input_ingress_facade &data) {
    bool probe_added = false;
    const gpointer probe_user_data = reinterpret_cast<gpointer>(&data.instance());

    // Add probe after parser (src pad) - to see what comes out
    if (elements.parser) {
        GstPad *const src_pad = gst_element_get_static_pad(elements.parser, "src");
        if (src_pad) {
            const gulong probe_id = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, parser_output_probe,
                                                      probe_user_data, nullptr);
            if (probe_id != 0) {
                probe_added = true;
                sf::debug(udp_frame_tracking, "Added frame tracking probe after parser (output)");
            } else {
                sf::error(udp_frame_tracking, "Failed to add frame tracking probe after parser");
            }

            gst_object_unref(src_pad);
        }
    }

    // Add probe before queue-before-decoder (sink pad) - to see if frames reach the queue
    if (elements.queue_before_decoder) {
        GstPad *const sink_pad = gst_element_get_static_pad(elements.queue_before_decoder, "sink");
        if (sink_pad) {
            const gulong probe_id = gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                                                      queue_before_decoder_input_probe, probe_user_data, nullptr);
            if (probe_id != 0) {
                probe_added = true;
                sf::debug(udp_frame_tracking, "Added frame tracking probe before queue-before-decoder (input)");
            } else {
                sf::error(udp_frame_tracking, "Failed to add frame tracking probe before queue-before-decoder");
            }
            gst_object_unref(sink_pad);
        }
    }

    // Add probe after queue-before-decoder (src pad) - to see if frames exit the queue
    if (elements.queue_before_decoder) {
        GstPad *const src_pad = gst_element_get_static_pad(elements.queue_before_decoder, "src");
        if (src_pad) {
            const gulong probe_id = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                                                      queue_before_decoder_output_probe, probe_user_data, nullptr);
            if (probe_id != 0) {
                probe_added = true;
                sf::debug(udp_frame_tracking, "Added frame tracking probe after queue-before-decoder (output)");
            } else {
                sf::error(udp_frame_tracking, "Failed to add frame tracking probe after queue-before-decoder");
            }
            gst_object_unref(src_pad);
        }
    }

    // Add probe before decoder (sink pad) - to see if frames reach decoder
    if (elements.decoder) {
        GstPad *const sink_pad = gst_element_get_static_pad(elements.decoder, "sink");
        if (sink_pad) {
            const gulong probe_id = gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, decoder_input_probe,
                                                      probe_user_data, nullptr);
            if (probe_id != 0) {
                probe_added = true;
                sf::debug(udp_frame_tracking, "Added frame tracking probe before decoder (input)");
            } else {
                sf::error(udp_frame_tracking, "Failed to add frame tracking probe before decoder");
            }
            gst_object_unref(sink_pad);
        }
    }

    // Add probe after decoder (src pad) - to see if decoder outputs frames
    if (elements.decoder) {
        GstPad *const src_pad = gst_element_get_static_pad(elements.decoder, "src");
        if (src_pad) {
            const gulong probe_id = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, decoder_output_probe,
                                                      probe_user_data, nullptr);
            if (probe_id != 0) {
                probe_added = true;
                sf::debug(udp_frame_tracking, "Added frame tracking probe after decoder (output)");
            } else {
                sf::error(udp_frame_tracking, "Failed to add frame tracking probe after decoder");
            }

            // Add probe to log full decoded frames and implement adaptive frame dropping
            // Uses the facade instance() raw pointer (escape hatch) for FPS monitoring and configuration
            const gulong full_frame_probe_id = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                                                                 parser_full_frame_probe, probe_user_data, nullptr);
            if (full_frame_probe_id != 0) {
                probe_added = true;
                if (data.params.performance.enable_frame_dropping) {
                    sf::info(udp_frame_tracking,
                             "Added full frame probe after decoder with adaptive frame dropping enabled "
                             "(target_fps=%.2f, smoothing_factor=%.2f)",
                             data.params.performance.target_fps, data.params.performance.drop_smoothing_factor);
                } else {
                    sf::info(udp_frame_tracking,
                             "Added full frame probe after decoder - will log complete decoded frames");
                }
            } else {
                sf::error(udp_frame_tracking, "Failed to add full frame probe after decoder");
            }

            gst_object_unref(src_pad);
        }
    }

    if (probe_added) {
        sf::debug(udp_frame_tracking,
                  "UDP frame tracking probes setup successfully - will log frame flow at key pipeline points");
    } else {
        sf::error(udp_frame_tracking,
                  "Failed to add any UDP frame tracking probes - frame flow diagnostics unavailable");
    }

    return probe_added;
}
