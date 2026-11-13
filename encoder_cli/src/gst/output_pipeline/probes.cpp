// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "probes.h"
#include "../../../libvcai/include/communication/transport.h"
#include "../../../libvcai/include/gst_raii.h"
#include "../../../libvcai/include/logging.h"
#include "../../../libvcai/include/pipeline_fps_monitor.h"
#include "../../encoder_enums.h"
#include "../../global_data.h"
#include "../../processing/branch_fps_tracker.h"
#include "../../reporting/performance_monitor.h"
#include "../../reporting/simple_status_reporter.h"
#include "output_pipeline_debug.h"

#include <gst/gst.h>

/**
 * @file probes.cpp
 * @brief Implementation of output pipeline pad probes for frame tracking.
 */

namespace {
    /**
     * @brief Pad probe callback to track frames processed by the output pipeline.
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn output_pipeline_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto *data = static_cast<global_data *>(user_data);
            if (data && data->monitoring.status_reporter) {
                data->monitoring.status_reporter->update_output_processed();
            }
            // Record end time for total pipeline monitoring (once per frame at tee output)
            if (data && data->monitoring.perf_monitor) {
                GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                if (buffer) {
                    const GstClockTime pts = GST_BUFFER_PTS(buffer);
                    if (pts != GST_CLOCK_TIME_NONE) {
                        data->monitoring.perf_monitor->record_end_time(monitoring_stage::total_pipeline, pts);
                    }
                }
            }
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames processed by the display branch.
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn output_display_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto *data = static_cast<global_data *>(user_data);
            if (data && data->monitoring.status_reporter) {
                data->monitoring.status_reporter->update_output_display();
            }
            // Track FPS for display branch
            update_branch_fps("display");
            // Record frame for pipeline FPS monitoring
            if (data) {
                data->monitoring.fps_monitor.record_frame("display");
            }
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to debug display decoder input.
     * @param pad The pad that received the buffer.
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn display_decoder_input_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                                                  gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
            if (buffer) {
                auto *data = static_cast<global_data *>(user_data);
                const bool is_random_access = data && data->params.packer_type_value == packer_type::random_access;

                GstClockTime pts = GST_BUFFER_PTS(buffer);
                GstClockTime dts = GST_BUFFER_DTS(buffer);
                GstClockTime duration = GST_BUFFER_DURATION(buffer);

                // Only fix corrupted timestamps in random_access mode
                // In low_delay mode, timestamps should be valid and we don't want to interfere
                if (is_random_access && data != nullptr) {
                    auto &pr = data->output_probe_runtime;
                    pr.random_access_timestamp_fix_frame_count++;
                    const int frame_count = pr.random_access_timestamp_fix_frame_count;

                    // Check if timestamps are corrupted (unreasonably large)
                    const bool pts_corrupted = pts >= GST_CLOCK_TIME_NONE / 2;
                    const bool dts_corrupted = dts >= GST_CLOCK_TIME_NONE / 2;

                    if (pts_corrupted || dts_corrupted) {
                        // Initialize base_time on first valid frame or first corrupted frame
                        if (pr.random_access_timestamp_base == GST_CLOCK_TIME_NONE) {
                            pr.random_access_timestamp_base = 0;
                        }

                        // Use frame duration (33.33ms for 30fps) or default
                        if (duration == GST_CLOCK_TIME_NONE || duration >= GST_CLOCK_TIME_NONE / 2) {
                            duration = static_cast<GstClockTime>(GST_SECOND / 30); // Default to 30fps
                        }

                        // Generate valid timestamps based on frame count
                        const GstClockTime valid_pts =
                                pr.random_access_timestamp_base + static_cast<GstClockTime>(frame_count - 1) * duration;
                        const GstClockTime valid_dts = valid_pts; // No reordering for display

                        sf::info(output_pipeline,
                                 "Fixing corrupted timestamps frame #%d: PTS=%" GST_TIME_FORMAT "->%" GST_TIME_FORMAT
                                 ", DTS=%" GST_TIME_FORMAT "->%" GST_TIME_FORMAT,
                                 frame_count, GST_TIME_ARGS(pts), GST_TIME_ARGS(valid_pts), GST_TIME_ARGS(dts),
                                 GST_TIME_ARGS(valid_dts));

                        pts = valid_pts;
                        dts = valid_dts;
                        GST_BUFFER_PTS(buffer) = pts;
                        GST_BUFFER_DTS(buffer) = dts;
                        GST_BUFFER_DURATION(buffer) = duration;
                    } else {
                        // Timestamps are valid, but update base_time for continuity
                        if (pr.random_access_timestamp_base == GST_CLOCK_TIME_NONE) {
                            pr.random_access_timestamp_base = pts;
                        }
                    }
                }

                if (data != nullptr) {
                    data->output_probe_runtime.display_decoder_input_log_frame_count++;
                    const int log_count = data->output_probe_runtime.display_decoder_input_log_frame_count;
                    sf::info(output_pipeline,
                             "Display decoder INPUT frame #%d: PTS=%" GST_TIME_FORMAT ", DTS=%" GST_TIME_FORMAT
                             ", duration=%" GST_TIME_FORMAT,
                             log_count, GST_TIME_ARGS(pts), GST_TIME_ARGS(dts), GST_TIME_ARGS(duration));
                }
            }
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames processed by the H.265 file branch.
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn output_h265_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto *data = static_cast<global_data *>(user_data);
            if (data && data->monitoring.status_reporter) {
                data->monitoring.status_reporter->update_output_h265();
            }
            // Track FPS for file branch
            update_branch_fps("file");
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames processed by the MP4 branch.
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn output_mp4_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto *data = static_cast<global_data *>(user_data);
            if (data && data->monitoring.status_reporter) {
                data->monitoring.status_reporter->update_output_mp4();
            }
            // Track FPS for mp4 branch
            update_branch_fps("mp4");
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames processed by the debug branch.
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn output_debug_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto *data = static_cast<global_data *>(user_data);
            if (data && data->monitoring.status_reporter) {
                data->monitoring.status_reporter->update_output_debug();
            }
            // Track FPS for debug branch
            update_branch_fps("debug");
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames processed by the RTP branch.
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn output_rtp_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto *data = static_cast<global_data *>(user_data);
            if (data && data->monitoring.status_reporter) {
                data->monitoring.status_reporter->update_output_rtp();
            }
            // Track FPS for RTP branch
            update_branch_fps("rtp");
            // Record frame for pipeline FPS monitoring
            if (data) {
                data->monitoring.fps_monitor.record_frame("rtp");
            }
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to track frames processed by the WebRTC branch.
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn output_webrtc_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            auto *data = static_cast<global_data *>(user_data);
            // Track FPS for WebRTC branch
            update_branch_fps("webrtc");
            // Record frame for pipeline FPS monitoring
            if (data) {
                data->monitoring.fps_monitor.record_frame("webrtc");
            }
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback to debug tee output (before distribution to branches).
     * @param pad The pad that received the buffer.
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the global_data structure.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn tee_output_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
            GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
            auto *data = static_cast<global_data *>(user_data);
            if (buffer && data != nullptr) {
                const GstClockTime pts = GST_BUFFER_PTS(buffer);
                const GstClockTime dts = GST_BUFFER_DTS(buffer);
                const GstClockTime duration = GST_BUFFER_DURATION(buffer);
                data->output_probe_runtime.tee_display_queue_sink_log_frame_count++;
                const int frame_count = data->output_probe_runtime.tee_display_queue_sink_log_frame_count;
                // Only log first 20 frames and frames with invalid PTS
                if (frame_count <= 20 || pts >= GST_CLOCK_TIME_NONE / 2) {
                    sf::info(output_pipeline,
                             "Display queue INPUT frame #%d: PTS=%" GST_TIME_FORMAT ", DTS=%" GST_TIME_FORMAT
                             ", duration=%" GST_TIME_FORMAT,
                             frame_count, GST_TIME_ARGS(pts), GST_TIME_ARGS(dts), GST_TIME_ARGS(duration));
                }
            }
        }
        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Adds pad probe to a queue element's sink pad.
     * @param queue_name Queue element name.
     * @param pipeline Output pipeline.
     * @param probe_func Probe callback function.
     * @param user_data User data for probe.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool add_probe_to_queue(const char *queue_name, GstElement *pipeline, GstPadProbeCallback probe_func,
                                          gpointer user_data) {
        GstElement *queue = gst_bin_get_by_name(GST_BIN(pipeline), queue_name);
        if (!queue) {
            return false;
        }

        GstPad *pad = gst_element_get_static_pad(queue, "sink");
        if (!pad) {
            gst_object_unref(queue);
            return false;
        }

        const gulong probe_id = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, probe_func, user_data, nullptr);
        const bool success = probe_id != 0;

        if (success) {
            sf::debug(output_pipeline, "Added pad probe to %s branch queue", queue_name);
        }

        gst_object_unref(pad);
        gst_object_unref(queue);
        return success;
    }

    /**
     * @brief Adds pad probe to tee sink pad.
     * @param tee Tee element.
     * @param probe_func Probe callback function.
     * @param user_data User data for probe.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool add_probe_to_tee_sink(GstElement *tee, GstPadProbeCallback probe_func, gpointer user_data) {
        GstPad *sink_pad = gst_element_get_static_pad(tee, "sink");
        if (!sink_pad) {
            return false;
        }

        const gulong probe_id = gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, probe_func, user_data, nullptr);
        const bool success = probe_id != 0;

        if (success) {
            sf::debug(output_pipeline, "Added pad probe to tee sink pad");
        }

        gst_object_unref(sink_pad);
        return success;
    }
} // namespace

namespace {
    /**
     * @brief Sets up tee sink probe.
     * @param tee Tee element.
     * @param user_data User data pointer.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool setup_tee_probe(GstElement *tee, gpointer user_data) {
        return add_probe_to_tee_sink(tee, output_pipeline_probe, user_data);
    }

    /**
     * @brief Sets up display branch probes.
     * @param data The narrow output branch state view.
     * @param user_data User data pointer.
     * @return true if any probe was added, false otherwise.
     */
    [[nodiscard]] bool setup_display_branch_probes(output_branch_facade &data, gpointer user_data) {
        if (!data.params.debug.enable_display_branch) {
            return false;
        }

        bool probe_added = false;

        // Add probe to display queue input to track timestamps before decoder
        GstElement *queue_display = gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "q-disp");
        if (queue_display) {
            GstPad *sink_pad = gst_element_get_static_pad(queue_display, "sink");
            if (sink_pad) {
                const gulong probe_id =
                        gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, tee_output_probe, user_data, nullptr);
                if (probe_id != 0) {
                    sf::info(output_pipeline, "Added debug probe to display queue input");
                    probe_added = true;
                }
                gst_object_unref(sink_pad);
            }
            gst_object_unref(queue_display);
        }

        if (add_probe_to_queue("q-disp", data.output_pipeline.get(), output_display_probe, user_data)) {
            probe_added = true;
        }

        // Add probe to decoder input and output to track frame flow
        GstElement *decoder = gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "d-disp");
        if (!decoder) {
            decoder = gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "d-disp-sw");
        }
        if (decoder) {
            // Add probe to decoder input (sink pad) - only in random_access mode to fix corrupted timestamps
            // In low_delay mode, timestamps should be valid and we don't need this fix
            if (data.params.packer_type_value == packer_type::random_access) {
                GstPad *sink_pad = gst_element_get_static_pad(decoder, "sink");
                if (sink_pad) {
                    const gulong probe_id = gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                                                              display_decoder_input_probe, user_data, nullptr);
                    if (probe_id != 0) {
                        sf::info(output_pipeline,
                                 "Added timestamp fix probe to display decoder input (random_access mode)");
                        probe_added = true;
                    }
                    gst_object_unref(sink_pad);
                }
            }
            gst_object_unref(decoder);
        }

        return probe_added;
    }

    /**
     * @brief Sets up H.265 branch probe.
     * @param data The narrow output branch state view.
     * @param user_data User data pointer.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool setup_h265_branch_probe(output_branch_facade &data, gpointer user_data) {
        if (!data.params.debug.enable_h265_branch) {
            return false;
        }
        return add_probe_to_queue("q-file", data.output_pipeline.get(), output_h265_probe, user_data);
    }

    /**
     * @brief Sets up MP4 branch probe.
     * @param data The narrow output branch state view.
     * @param user_data User data pointer.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool setup_mp4_branch_probe(output_branch_facade &data, gpointer user_data) {
        if (!data.params.debug.enable_mp4_branch) {
            return false;
        }
        return add_probe_to_queue("q-mp4", data.output_pipeline.get(), output_mp4_probe, user_data);
    }

    /**
     * @brief Sets up debug branch probe.
     * @param data The narrow output branch state view.
     * @param user_data User data pointer.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool setup_debug_branch_probe(output_branch_facade &data, gpointer user_data) {
        if (!data.params.debug.save_frames) {
            return false;
        }
        return add_probe_to_queue("q-debug", data.output_pipeline.get(), output_debug_probe, user_data);
    }

    /**
     * @brief Sets up RTP branch probe.
     * @param data The narrow output branch state view.
     * @param user_data User data pointer.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool setup_rtp_branch_probe(output_branch_facade &data, gpointer user_data) {
        return add_probe_to_queue("q-rtp", data.output_pipeline.get(), output_rtp_probe, user_data);
    }

    /**
     * @brief Sets up WebRTC branch probe.
     * @param data The narrow output branch state view.
     * @param user_data User data pointer.
     * @return true if probe was added, false otherwise.
     */
    [[nodiscard]] bool setup_webrtc_branch_probe(output_branch_facade &data, gpointer user_data) {
        if (data.params.communication.resolved_transport_type() != sf::communication::transport_type::webrtc) {
            return false;
        }

        GstElement *webrtcbin = gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "webrtcbin");
        if (!webrtcbin) {
            return false;
        }

        GstPad *sink_pad = gst_element_get_static_pad(webrtcbin, "sink");
        if (!sink_pad) {
            gst_object_unref(webrtcbin);
            return false;
        }

        const gulong probe_id =
                gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, output_webrtc_probe, user_data, nullptr);
        const bool success = probe_id != 0;

        if (success) {
            sf::debug(output_pipeline, "Added pad probe to WebRTC branch");
        }

        gst_object_unref(sink_pad);
        gst_object_unref(webrtcbin);
        return success;
    }
} // namespace

bool setup_output_pipeline_probes(output_branch_facade &data) {
    if (!data.output_pipeline) {
        sf::error(output_pipeline, "Cannot setup output pipeline probes: output pipeline is null");
        return false;
    }

    GstElement *tee = gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "video-tee");
    if (!tee) {
        sf::error(output_pipeline, "Cannot setup output pipeline probes: tee element not found");
        return false;
    }

    bool probe_added = false;
    // Probes read the mutable per-instance runtime and monitoring state, which GStreamer pad
    // probes require through a stable raw pointer; the facade's instance() is that escape hatch.
    gpointer user_data = &data.instance();

    if (setup_tee_probe(tee, user_data)) {
        probe_added = true;
    }

    if (setup_display_branch_probes(data, user_data)) {
        probe_added = true;
    }

    if (setup_h265_branch_probe(data, user_data)) {
        probe_added = true;
    }

    if (setup_mp4_branch_probe(data, user_data)) {
        probe_added = true;
    }

    if (setup_debug_branch_probe(data, user_data)) {
        probe_added = true;
    }

    if (setup_rtp_branch_probe(data, user_data)) {
        probe_added = true;
    }

    if (setup_webrtc_branch_probe(data, user_data)) {
        probe_added = true;
    }

    gst_object_unref(tee);

    if (probe_added) {
        sf::info(
                output_pipeline,
                "Output pipeline probes setup successfully: Tracking frames processed by output pipeline and branches");
    } else {
        sf::warning(output_pipeline, "No pad probes were added to output pipeline");
    }

    return probe_added;
}
