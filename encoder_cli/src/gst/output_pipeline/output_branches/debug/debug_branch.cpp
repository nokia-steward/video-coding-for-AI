// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "debug_branch.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../../../libvcai/include/packed_regions_info.h"
#include "../../../../../libvcai/include/rectangle.h"
#include "../../../../encoder_enums.h"
#include "../../../../global_data.h"
#include "../../../pipeline_utils.h"
#include "../../elements.h"
#include "../../output_pipeline_debug.h"
#include "../../output_roi_sequence_correlation.h"
#include "../../paths.h"
#include "../output_branch_facade.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <mutex>
#include <vector>

/**
 * @file debug_branch.cpp
 * @brief Implementation of debug branch creation and linking.
 */

namespace {
    // -----------------------------------------------------------------------
    // Software border drawing for debug frames
    // -----------------------------------------------------------------------

    /**
     * @brief Extracts rectangles from packed_regions_info for border drawing.
     *
     * Mirrors the logic in the display branch (get_packed_rectangles_from_pri)
     * but omits the RTP timestamp filtering which is not relevant for saved
     * debug frames.
     *
     * @param pri The packed regions info.
     * @return Vector of rectangles to draw.
     */
    [[nodiscard]] std::vector<sf::rectangle> extract_rectangles(const sf::packed_regions_info &pri) {
        if (pri.cancel_flag || pri.regions.empty()) {
            return {};
        }

        const uint8_t log2_unit = pri.log2_unit_size.value_or(0);
        const int unit = static_cast<int>(1U << log2_unit);
        std::vector<sf::rectangle> rects;
        rects.reserve(pri.regions.size());

        for (const auto &r: pri.regions) {
            if (!r.top_left_in_units_x.has_value() || !r.top_left_in_units_y.has_value() ||
                !r.width_in_units_minus1.has_value() || !r.height_in_units_minus1.has_value()) {
                continue;
            }
            const int left = static_cast<int>(r.top_left_in_units_x.value()) * unit;
            const int top = static_cast<int>(r.top_left_in_units_y.value()) * unit;
            const int rect_width = (static_cast<int>(r.width_in_units_minus1.value()) + 1) * unit;
            const int rect_height = (static_cast<int>(r.height_in_units_minus1.value()) + 1) * unit;
            rects.emplace_back(left, top, rect_width, rect_height);
        }
        return rects;
    }

    /**
     * @brief Draws a single-pixel horizontal line on an I420 Y plane.
     */
    void draw_hline_y(uint8_t *y_plane, int luma_stride, int frame_w, int frame_h, int x0, int x1, int y,
                      uint8_t luma) {
        if (y < 0 || y >= frame_h)
            return;
        x0 = std::max(x0, 0);
        x1 = std::min(x1, frame_w - 1);
        for (int x = x0; x <= x1; ++x) {
            y_plane[y * luma_stride + x] = luma;
        }
    }

    /**
     * @brief Draws a single-pixel vertical line on an I420 Y plane.
     */
    void draw_vline_y(uint8_t *y_plane, int luma_stride, int frame_w, int frame_h, int x, int y0, int y1,
                      uint8_t luma) {
        if (x < 0 || x >= frame_w)
            return;
        y0 = std::max(y0, 0);
        y1 = std::min(y1, frame_h - 1);
        for (int y = y0; y <= y1; ++y) {
            y_plane[y * luma_stride + x] = luma;
        }
    }

    /**
     * @brief Draws a rectangle border on a GstVideoFrame (I420 luma plane only).
     *
     * Draws a 2-pixel-wide green border.  Only the Y plane is modified (luma is
     * set to a bright value); the chroma planes are left untouched, which results
     * in a bright green/cyan border on typical YUV content and avoids worrying
     * about the exact chroma subsampling layout.
     */
    void draw_rect_border_on_frame(GstVideoFrame *frame, const sf::rectangle &rect, int border_width) {
        const int frame_w = GST_VIDEO_FRAME_WIDTH(frame);
        const int frame_h = GST_VIDEO_FRAME_HEIGHT(frame);
        auto *y_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));
        const int luma_stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);

        // Bright luma for the border (close to white — shows up clearly on any content)
        constexpr uint8_t border_luma = 220;

        for (int b = 0; b < border_width; ++b) {
            // Top edge
            draw_hline_y(y_plane, luma_stride, frame_w, frame_h, rect.left + b, rect.left + rect.width - 1 - b,
                         rect.top + b, border_luma);
            // Bottom edge
            draw_hline_y(y_plane, luma_stride, frame_w, frame_h, rect.left + b, rect.left + rect.width - 1 - b,
                         rect.top + rect.height - 1 - b, border_luma);
            // Left edge
            draw_vline_y(y_plane, luma_stride, frame_w, frame_h, rect.left + b, rect.top + b,
                         rect.top + rect.height - 1 - b, border_luma);
            // Right edge
            draw_vline_y(y_plane, luma_stride, frame_w, frame_h, rect.left + rect.width - 1 - b, rect.top + b,
                         rect.top + rect.height - 1 - b, border_luma);
        }
    }

    /**
     * @brief Pad probe that draws ROI borders on debug branch frames in software.
     *
     * This probe is attached between the software decoder (avdec_h265) and
     * videoconvert, where the frame is in a planar YUV format (typically I420).
     * It pops one packed_regions_info from the debug_regions_queue per frame and
     * draws the corresponding rectangles directly on the Y plane.
     */
    [[nodiscard]] std::optional<metadata_frame_bundle> pop_debug_bundle_for_sequence(global_data &data,
                                                                                     const uint64_t sequence_id) {
        auto &reorder = data.output_debug_runtime.bundle_reorder;
        {
            std::lock_guard lock(reorder.mutex);
            const auto it = reorder.pending.find(sequence_id);
            if (it != reorder.pending.end()) {
                metadata_frame_bundle bundle = std::move(it->second);
                reorder.pending.erase(it);
                return bundle;
            }
        }

        while (true) {
            auto bundle_opt = data.debug_regions_queue.try_pop();
            if (!bundle_opt.has_value()) {
                return std::nullopt;
            }
            data.metadata_runtime.record_bundle_dequeued();
            if (bundle_opt->sequence_id == sequence_id) {
                return bundle_opt;
            }

            std::lock_guard lock(reorder.mutex);
            reorder.pending[bundle_opt->sequence_id] = std::move(*bundle_opt);
        }
    }

    [[nodiscard]] std::optional<metadata_frame_bundle> try_pop_next_debug_regions_bundle_fifo(global_data &data) {
        auto bundle_opt = data.debug_regions_queue.try_pop();
        if (!bundle_opt.has_value()) {
            return std::nullopt;
        }
        data.metadata_runtime.record_bundle_dequeued();
        return bundle_opt;
    }

    GstPadProbeReturn debug_border_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
            return GST_PAD_PROBE_OK;
        }

        auto &data = *static_cast<global_data *>(user_data);

        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }

        const GstClockTime pts = GST_BUFFER_PTS(buffer);
        const auto resolution = resolve_decoded_branch_roi_sequence_id(
                pts, buffer, data.metadata_runtime, data.output_debug_runtime.debug_roi_correlation_next_sequence);

        if (!resolution.used_pts_map) {
            const int remaining = data.output_debug_runtime.debug_roi_pts_correlation_debug_logs_remaining.fetch_sub(
                    1, std::memory_order_relaxed);
            if (remaining > 0) {
                sf::debug(output_pipeline,
                          "Debug ROI borders: encoded-branch PTS map miss (pts=%" GST_TIME_FORMAT
                          "); correlating via %s",
                          GST_TIME_ARGS(pts),
                          resolution.used_frame_contract_meta ? "frame_contract GstMeta" : "strict output sequence");
            }
        }
        if (resolution.used_strict_frame_order) {
            if (!data.output_debug_runtime.warned_strict_order_roi_correlation.exchange(true)) {
                sf::warning(output_pipeline,
                            "Debug ROI borders: using strict output sequence order because decoded-buffer PTS did not "
                            "match SEI-path registration (frame drops may desynchronize saved debug frames)");
            }
        }

        std::optional<metadata_frame_bundle> bundle_opt;
        if (resolution.used_strict_frame_order) {
            bundle_opt = try_pop_next_debug_regions_bundle_fifo(data);
        } else {
            bundle_opt = pop_debug_bundle_for_sequence(data, resolution.sequence_id);
        }
        if (!bundle_opt.has_value()) {
            if (resolution.used_strict_frame_order) {
                const int remaining =
                        data.output_debug_runtime.debug_roi_strict_fifo_miss_debug_logs_remaining.fetch_sub(
                                1, std::memory_order_relaxed);
                if (remaining > 0) {
                    sf::debug(output_pipeline,
                              "Debug ROI borders: no debug_regions_queue bundle available (FIFO strict-order path); "
                              "skipping borders for this frame");
                }
                return GST_PAD_PROBE_OK;
            }
            data.metadata_runtime.record_validation_failure();
            data.request_metadata_contract_fail_stop("debug branch missing metadata bundle");
            return GST_PAD_PROBE_OK;
        }

        advance_decoded_branch_roi_sequence_cursor(data.output_debug_runtime.debug_roi_correlation_next_sequence,
                                                   bundle_opt->sequence_id);

        if (bundle_opt->regions.cancel_flag) {
            return GST_PAD_PROBE_OK;
        }

        const auto rects = extract_rectangles(bundle_opt->regions);
        if (rects.empty()) {
            return GST_PAD_PROBE_OK;
        }

        // Make buffer writable
        if (!gst_buffer_is_writable(buffer)) {
            buffer = gst_buffer_make_writable(buffer);
            if (!buffer) {
                return GST_PAD_PROBE_OK;
            }
            GST_PAD_PROBE_INFO_DATA(info) = buffer;
        }

        // Map the frame for writing
        GstCaps *caps = gst_pad_get_current_caps(pad);
        if (!caps) {
            return GST_PAD_PROBE_OK;
        }
        GstVideoInfo vinfo;
        if (!gst_video_info_from_caps(&vinfo, caps)) {
            gst_caps_unref(caps);
            return GST_PAD_PROBE_OK;
        }
        gst_caps_unref(caps);

        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &vinfo, buffer, GST_MAP_READWRITE)) {
            return GST_PAD_PROBE_OK;
        }

        constexpr int border_width = 2;
        for (const auto &rect: rects) {
            draw_rect_border_on_frame(&frame, rect, border_width);
        }

        gst_video_frame_unmap(&frame);
        return GST_PAD_PROBE_OK;
    }

    // -----------------------------------------------------------------------
    // Debug branch pipeline elements
    // -----------------------------------------------------------------------

    /**
     * @brief Check if debug branch should be enabled.
     * @param data The narrow output branch state view.
     * @return true if debug branch should be enabled, false otherwise.
     */
    [[nodiscard]] bool is_debug_branch_enabled(const output_branch_facade &data) {
        return data.params.debug.save_frames;
    }

    /**
     * @brief Creates debug branch elements.
     * @param elements Output structure to populate.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_debug_branch_elements(output_pipeline_elements &elements) {
        elements.queue_debug = gst_element_factory_make("queue", "q-debug");
        if (!elements.queue_debug) {
            sf::error(output_pipeline, "Failed to create queue for debug branch");
            return false;
        }

        elements.parser_debug = gst_element_factory_make("h265parse", "p-debug");
        if (!elements.parser_debug) {
            sf::error(output_pipeline, "Failed to create h265parse for debug branch");
            return false;
        }

        // Use the software FFmpeg-based H.265 decoder for the debug branch.
        // The GPU decoder (nvh265dec) causes CUDA context conflicts when it runs
        // in the same process as the GPU encoder, leading to a deadlock where the
        // output pipeline never reaches PLAYING state.  The software decoder
        // avoids this entirely and is perfectly adequate for saving debug frames.
        elements.decoder_debug = gst_element_factory_make("avdec_h265", "d-debug");
        if (!elements.decoder_debug) {
            sf::error(output_pipeline, "Failed to create avdec_h265 for debug branch");
            return false;
        }

        // Software videoconvert handles pixel format conversion (e.g. I420 -> RGB)
        // between the software decoder output and pngenc input.
        elements.converter_debug = gst_element_factory_make("videoconvert", "c-debug");
        if (!elements.converter_debug) {
            sf::error(output_pipeline, "Failed to create videoconvert for debug branch");
            return false;
        }

        elements.encoder_debug = gst_element_factory_make("pngenc", "e-debug");
        if (!elements.encoder_debug) {
            sf::error(output_pipeline, "Failed to create pngenc for debug branch");
            return false;
        }

        elements.sink_debug = gst_element_factory_make("multifilesink", "s-debug");
        if (!elements.sink_debug) {
            sf::error(output_pipeline, "Failed to create multifilesink for debug branch");
            return false;
        }

        return true;
    }

    /**
     * @brief Configures debug branch elements.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    [[nodiscard]] bool configure_debug_branch_elements(const output_branch_facade &data,
                                                       output_pipeline_elements &elements) {
        // Configure debug branch queue.
        // The debug branch contains a heavyweight decode + PNG-encode chain that
        // is much slower than the main encoding path.  Using leak_downstream
        // prevents the debug branch from back-pressuring the tee and deadlocking
        // the entire output pipeline when it cannot keep up.
        if (elements.queue_debug) {
            const int effective_framerate = data.params.framerate > 0 ? data.params.framerate : 60;
            const int max_buffers = 2 * effective_framerate;
            const int leaky_mode = static_cast<int>(queue_leaky_mode::leak_downstream);
            g_object_set(elements.queue_debug, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured debug branch queue: max-size-buffers=%d, leaky=downstream",
                      max_buffers);
        }

        // Configure debug frame sink path
        if (elements.sink_debug) {
            const std::filesystem::path debug_dir_path =
                    resolve_frame_output_root_directory(data.params) / "debug_frames";
            const std::string debug_dir = debug_dir_path.string();

            // Create debug frames directory
            try {
                if (std::filesystem::exists(debug_dir)) {
                    sf::info(output_pipeline, "Clearing existing debug frames from %s", debug_dir.c_str());
                    for (const auto &entry: std::filesystem::directory_iterator(debug_dir)) {
                        if (std::filesystem::is_regular_file(entry.path())) {
                            std::filesystem::remove(entry.path());
                        }
                    }
                }
                if (std::filesystem::create_directories(debug_dir)) {
                    sf::info(output_pipeline, "Created %s directory for processed frame saving", debug_dir.c_str());
                }
            } catch (const std::filesystem::filesystem_error &e) {
                sf::warning(output_pipeline, "Failed to create %s directory: %s", debug_dir.c_str(), e.what());
                return false;
            }

            const std::string debug_location = debug_dir + "/frame_%06d.png";
            g_object_set(elements.sink_debug, "location", debug_location.c_str(), nullptr);
            g_object_set(elements.sink_debug, "max-files", 0, nullptr);

            // async=false tells GStreamer that this sink does not need to preroll
            // before the pipeline transitions to PLAYING.  Without this, the
            // avdec_h265 decoder waits for data during the PAUSED→PLAYING
            // transition, but no data can flow until the encoding pipeline is
            // running — creating a deadlock that keeps the output pipeline at
            // PAUSED and prevents any frames from being processed.
            g_object_set(elements.sink_debug, "async", FALSE, nullptr);
        }

        return true;
    }

    /**
     * @brief Helper to link two GStreamer elements with diagnostic logging on failure.
     * @param src Source element.
     * @param dst Destination element.
     * @return true if linking succeeded, false otherwise.
     */
    [[nodiscard]] bool link_or_log(GstElement *src, GstElement *dst) {
        if (src == nullptr || dst == nullptr) {
            sf::error(output_pipeline, "Debug branch link failed: src=%p dst=%p", static_cast<void *>(src),
                      static_cast<void *>(dst));
            return false;
        }
        if (gst_element_link(src, dst) == TRUE) {
            return true;
        }
        const gchar *src_name = gst_element_get_name(src);
        const gchar *dst_name = gst_element_get_name(dst);
        sf::error(output_pipeline, "Debug branch link failed: '%s' -> '%s'", src_name ? src_name : "(null)",
                  dst_name ? dst_name : "(null)");
        g_free(const_cast<gchar *>(src_name));
        g_free(const_cast<gchar *>(dst_name));
        return false;
    }

    /**
     * @brief Links debug branch elements.
     *
     * Pipeline: queue -> h265parse -> avdec_h265 -> videoconvert -> pngenc -> multifilesink
     *
     * Uses an all-software decode path to avoid CUDA context conflicts with the
     * GPU encoder running in the same process.
     *
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_debug_branch(const output_pipeline_elements &elements) {
        if (!link_or_log(elements.queue_debug, elements.parser_debug)) {
            return false;
        }
        if (!link_or_log(elements.parser_debug, elements.decoder_debug)) {
            return false;
        }
        if (!link_or_log(elements.decoder_debug, elements.converter_debug)) {
            return false;
        }
        if (!link_or_log(elements.converter_debug, elements.encoder_debug)) {
            return false;
        }
        return link_or_log(elements.encoder_debug, elements.sink_debug);
    }

    /**
     * @brief Links tee to debug branch.
     * @param tee The tee element.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_tee_to_debug_branch(GstElement *tee, const output_pipeline_elements &elements) {
        return link_tee_to_branch(tee, elements.queue_debug);
    }

    /**
     * @brief Adds debug branch elements to pipeline.
     * @param data The narrow output branch state view.
     * @param elements The pipeline elements.
     */
    void add_debug_branch_to_pipeline(const output_branch_facade &data, const output_pipeline_elements &elements) {
        gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_debug, elements.parser_debug,
                         elements.decoder_debug, elements.converter_debug, elements.encoder_debug, elements.sink_debug,
                         nullptr);
    }
} // namespace

output_branch_functions create_debug_branch_functions() {
    return output_branch_functions{
            .is_enabled = [](const output_branch_facade &data) { return is_debug_branch_enabled(data); },
            .create_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_debug_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return create_debug_branch_elements(elements);
                    },
            .configure_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_debug_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return configure_debug_branch_elements(data, elements);
                    },
            .link_branch =
                    [](const output_pipeline_elements &elements) {
                        if (!elements.queue_debug) {
                            return true; // Not created, but not an error
                        }
                        return link_debug_branch(elements);
                    },
            .link_to_tee =
                    [](GstElement *tee, const output_pipeline_elements &elements) {
                        if (!elements.queue_debug) {
                            return true; // Not created, but not an error
                        }
                        return link_tee_to_debug_branch(tee, elements);
                    },
            .add_to_pipeline =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (is_debug_branch_enabled(data)) {
                            add_debug_branch_to_pipeline(data, elements);
                        }
                    },
            .get_branch_name = []() { return "debug"; },
            .setup_signals =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        // Attach a pad probe to draw ROI borders on decoded frames before
                        // they are converted to RGB and saved as PNG.  The probe is placed
                        // on the decoder's src pad where the frame is still in planar YUV.
                        if (!elements.decoder_debug) {
                            return true; // Branch not created
                        }

                        GstPad *src_pad = gst_element_get_static_pad(elements.decoder_debug, "src");
                        if (!src_pad) {
                            sf::warning(output_pipeline,
                                        "Debug branch: could not get decoder src pad for border probe");
                            return true; // Non-fatal — frames will be saved without borders
                        }

                        const gulong probe_id = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                                                                  debug_border_probe, &data.instance(), nullptr);

                        if (probe_id == 0) {
                            sf::warning(output_pipeline, "Debug branch: failed to attach border drawing probe");
                        } else {
                            sf::info(output_pipeline, "Debug branch: border drawing probe attached to decoder output");
                        }

                        gst_object_unref(src_pad);
                        return true;
                    }};
}

output_branch_context create_debug_branch_context() {
    return output_branch_context{.functions = create_debug_branch_functions()};
}
