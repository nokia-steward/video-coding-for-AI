// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "display_branch.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../../../libvcai/include/nvosd_overlay.h"
#include "../../../../../libvcai/include/packed_regions_info.h"
#include "../../../../../libvcai/include/rectangle.h"
#include "../../../../../libvcai/include/text_drawer.h"
#include "../../../../../libvcai/include/yuv_color.h"
#include "../../../../encoder_enums.h"
#include "../../../../global_data.h"
#include "../../../../gst/gpu_elements.h"
#include "../../../../gst/input_types/rtp_timestamp_overlay.h"
#include "../../../pipeline_utils.h"
#include "../../elements.h"
#include "../../output_pipeline_debug.h"
#include "../../output_roi_sequence_correlation.h"
#include "../output_branch_facade.h"
#include "overlay_renderer.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <gst/gst.h>
#include <gstnvdsmeta.h>
#include <iomanip>
#include <mutex>
#include <nvdsmeta.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

/**
 * @file display_branch.cpp
 * @brief Implementation of display branch creation and linking.
 */

namespace {
    [[nodiscard]] const char *get_display_rgba_caps() {
        // Use RGBA format for nvosd to fix rendering artifacts on Jetson
        // RGBA provides proper alpha channel support for overlay compositing
        return "video/x-raw(memory:NVMM),format=RGBA";
    }

    struct display_overlay_flags {
        bool draw_fps = false;
        bool draw_bitrate = false;
        bool draw_bitrate_large = false;
    };

    [[nodiscard]] display_overlay_flags get_display_overlay_flags(const output_branch_facade &data) {
        return display_overlay_flags{.draw_fps = data.params.debug.draw_fps,
                                     .draw_bitrate = data.params.debug.draw_bitrate,
                                     .draw_bitrate_large = data.params.debug.draw_bitrate_large};
    }

    /**
     * @brief Calculates text position for bottom right corner with padding.
     * @param text_length Length of text string.
     * @param width Video width.
     * @param height Video height.
     * @return Pair of (x, y) coordinates or empty optional if position is invalid.
     */
    [[nodiscard]] std::optional<std::pair<int, int>> calculate_bitrate_text_position(size_t text_length, int width,
                                                                                     int height) {
        constexpr int padding_x = 24;
        constexpr int padding_y = 24;
        constexpr int safety_margin = 8;
        const int text_width = static_cast<int>(text_length) * (sf::TEXT_CHAR_WIDTH + 1) + safety_margin;
        const int start_x = width - text_width - padding_x;
        const int start_y = height - sf::TEXT_CHAR_HEIGHT - padding_y;

        // Only draw if position is valid (text fits on screen)
        if (start_x >= 0 && start_y >= 0 && start_y + sf::TEXT_CHAR_HEIGHT <= height) {
            return std::make_pair(start_x, start_y);
        }
        return std::nullopt;
    }

    /**
     * @brief Formats FPS value as a string (e.g., "30.0 fps").
     * @param fps The FPS value to format.
     * @return Formatted string or empty string if invalid.
     */
    [[nodiscard]] std::string format_fps_string(const double fps) {
        if (fps <= 0.0) {
            return {};
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << fps << " fps";
        return oss.str();
    }

    [[nodiscard]] std::optional<std::pair<int, int>> calculate_fps_text_position(size_t text_length, int width) {
        constexpr int padding_x = 24;
        constexpr int padding_y = 24;
        constexpr int safety_margin = 8;
        const int text_width = static_cast<int>(text_length) * (sf::TEXT_CHAR_WIDTH + 1) + safety_margin;
        const int start_x = width - text_width - padding_x;
        const int start_y = padding_y;

        if (start_x >= 0) {
            return std::make_pair(start_x, start_y);
        }
        return std::nullopt;
    }

    [[nodiscard]] bool ensure_display_border_dump_stream(global_data &data) {
        auto &rt = data.output_display_runtime;
        if (rt.border_dump_initialized) {
            return rt.border_dump_stream.is_open();
        }
        if (rt.border_dump_failed) {
            return false;
        }

        const std::filesystem::path dump_path =
                resolve_frame_output_root_directory(data.params) / "tracker_dump" / "display_border_rectangles.jsonl";
        std::error_code create_error;
        std::filesystem::create_directories(dump_path.parent_path(), create_error);
        if (create_error) {
            sf::warning(output_pipeline, "Failed to create display border dump directory '%s': %s",
                        dump_path.parent_path().string().c_str(), create_error.message().c_str());
            rt.border_dump_failed = true;
            return false;
        }

        rt.border_dump_stream.open(dump_path, std::ios::out | std::ios::trunc);
        if (!rt.border_dump_stream.is_open()) {
            sf::warning(output_pipeline, "Failed to open display border dump file '%s'", dump_path.c_str());
            rt.border_dump_failed = true;
            return false;
        }

        rt.border_dump_initialized = true;
        sf::info(output_pipeline, "Temporary display border dump enabled: %s", dump_path.c_str());
        return true;
    }

    void dump_display_border_rectangles(global_data &data, const guint64 frame_number, const GstClockTime pts,
                                        const int width, const int height,
                                        const std::vector<sf::rectangle> &rectangles) {
        if (!data.params.packer_logging) {
            return;
        }

        auto &rt = data.output_display_runtime;
        std::lock_guard lock(rt.border_dump_mutex);
        if (!ensure_display_border_dump_stream(data)) {
            return;
        }

        rt.border_dump_stream << "{\"frame_number\":" << frame_number << ",\"pts_ns\":";
        if (pts == GST_CLOCK_TIME_NONE) {
            rt.border_dump_stream << -1;
        } else {
            rt.border_dump_stream << static_cast<long long>(pts);
        }
        rt.border_dump_stream << ",\"frame_width\":" << width << ",\"frame_height\":" << height << ",\"rectangles\":[";
        for (size_t index = 0; index < rectangles.size(); ++index) {
            const auto &rect = rectangles[index];
            if (index > 0) {
                rt.border_dump_stream << ",";
            }
            rt.border_dump_stream << "{\"x\":" << rect.left << ",\"y\":" << rect.top << ",\"w\":" << rect.width
                                  << ",\"h\":" << rect.height << "}";
        }
        rt.border_dump_stream << "]}\n";
        rt.border_dump_stream.flush();
    }

    [[nodiscard]] std::optional<metadata_frame_bundle>
    pop_packed_regions_info_for_sequence(global_data &data, const uint64_t sequence_id) {
        auto &reorder = data.output_display_runtime.bundle_reorder;
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
            auto bundle_opt = data.display_regions_queue.try_pop();
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

    /**
     * @brief Pops the next display border metadata bundle in FIFO order without touching bundle_reorder.
     *
     * Used when decoded-buffer PTS/meta cannot bind to SEI registration; avoids stashing mismatched sequence ids in
     * `pending` and does not fail-stop when the queue is temporarily empty.
     */
    [[nodiscard]] std::optional<metadata_frame_bundle> try_pop_next_display_regions_bundle_fifo(global_data &data) {
        auto bundle_opt = data.display_regions_queue.try_pop();
        if (!bundle_opt.has_value()) {
            return std::nullopt;
        }
        data.metadata_runtime.record_bundle_dequeued();
        return bundle_opt;
    }

    [[nodiscard]] std::vector<sf::rectangle> get_packed_rectangles_from_pri(const sf::packed_regions_info &region_info,
                                                                            const int frame_width,
                                                                            const int frame_height,
                                                                            const bool filter_rtp_timestamp) {
        if (region_info.cancel_flag || region_info.regions.empty()) {
            return {};
        }

        const uint8_t log2_unit_size = region_info.log2_unit_size.value_or(0);
        const int unit_size = static_cast<int>(1U << log2_unit_size);
        std::vector<sf::rectangle> rectangles;
        rectangles.reserve(region_info.regions.size());
        const sf::rectangle timestamp_roi = filter_rtp_timestamp
                                                    ? create_timestamp_roi_rectangle(frame_width, frame_height)
                                                    : sf::rectangle{0, 0, 0, 0};

        for (const auto &region: region_info.regions) {
            if (!region.top_left_in_units_x.has_value() || !region.top_left_in_units_y.has_value() ||
                !region.width_in_units_minus1.has_value() || !region.height_in_units_minus1.has_value()) {
                continue;
            }

            const int left = static_cast<int>(region.top_left_in_units_x.value()) * unit_size;
            const int top = static_cast<int>(region.top_left_in_units_y.value()) * unit_size;
            const int width = (static_cast<int>(region.width_in_units_minus1.value()) + 1) * unit_size;
            const int height = (static_cast<int>(region.height_in_units_minus1.value()) + 1) * unit_size;

            int target_left = left;
            int target_top = top;
            int target_width = width;
            int target_height = height;

            if (region.target_region_top_left_in_units_x.has_value() &&
                region.target_region_top_left_in_units_y.has_value()) {
                target_left = static_cast<int>(region.target_region_top_left_in_units_x.value()) * unit_size;
                target_top = static_cast<int>(region.target_region_top_left_in_units_y.value()) * unit_size;
            }

            const unsigned int resampling_ratio_idx = region.resampling_ratio_idx.value_or(0U);
            const sf::resampling_ratio *ratio = nullptr;

            if (resampling_ratio_idx == 0U) {
                ratio = &region_info.inferred_resampling_ratio;
            } else {
                const size_t signaled_ratio_index = static_cast<size_t>(resampling_ratio_idx - 1U);
                if (signaled_ratio_index < region_info.resampling_ratios.size()) {
                    ratio = &region_info.resampling_ratios[signaled_ratio_index];
                }
            }

            if (ratio != nullptr) {
                const unsigned int width_num = ratio->width_num_minus1 + 1U;
                const unsigned int width_den = ratio->width_denom_minus1 + 1U;
                unsigned int height_num = width_num;
                unsigned int height_den = width_den;
                if (ratio->height_num_minus1.has_value() && ratio->height_denom_minus1.has_value()) {
                    height_num = ratio->height_num_minus1.value() + 1U;
                    height_den = ratio->height_denom_minus1.value() + 1U;
                }

                target_width = static_cast<int>((static_cast<unsigned int>(width) * width_num) / width_den);
                target_height = static_cast<int>((static_cast<unsigned int>(height) * height_num) / height_den);
            }

            if (filter_rtp_timestamp) {
                const sf::rectangle target_rect(target_left, target_top, target_width, target_height);
                const double overlap_ratio = target_rect.calculate_iou(timestamp_roi);
                if (overlap_ratio >= 0.6) {
                    continue;
                }
            }

            rectangles.emplace_back(left, top, width, height);
        }

        return rectangles;
    }

    [[nodiscard]] GstPadProbeReturn add_roi_borders_from_pri_probe(GstPad *pad, GstPadProbeInfo *info,
                                                                   gpointer user_data) {
        auto &data = *static_cast<global_data *>(user_data);

        if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
            return GST_PAD_PROBE_OK;
        }

        if (!data.params.debug.draw_borders) {
            return GST_PAD_PROBE_OK;
        }

        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (buffer == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        const GstClockTime pts = GST_BUFFER_PTS(buffer);
        const auto resolution = resolve_decoded_branch_roi_sequence_id(
                pts, buffer, data.metadata_runtime, data.output_display_runtime.display_roi_correlation_next_sequence);

        if (!resolution.used_pts_map) {
            const int remaining =
                    data.output_display_runtime.display_roi_pts_correlation_debug_logs_remaining.fetch_sub(
                            1, std::memory_order_relaxed);
            if (remaining > 0) {
                sf::debug(output_pipeline,
                          "Display ROI borders: encoded-branch PTS map miss (pts=%" GST_TIME_FORMAT
                          "); correlating via "
                          "%s",
                          GST_TIME_ARGS(pts),
                          resolution.used_frame_contract_meta ? "frame_contract GstMeta" : "strict output sequence");
            }
        }
        if (resolution.used_strict_frame_order) {
            if (!data.output_display_runtime.warned_strict_order_roi_correlation.exchange(true)) {
                sf::warning(output_pipeline,
                            "Display ROI borders: using strict output sequence order because decoded-buffer PTS did "
                            "not match SEI-path registration (frame drops may desynchronize overlays)");
            }
        }

        if (!gst_buffer_is_writable(buffer)) {
            buffer = gst_buffer_make_writable(buffer);
            if (buffer == nullptr) {
                return GST_PAD_PROBE_OK;
            }
            GST_PAD_PROBE_INFO_DATA(info) = buffer;
        }

        std::optional<metadata_frame_bundle> metadata_bundle_opt;
        if (resolution.used_strict_frame_order) {
            metadata_bundle_opt = try_pop_next_display_regions_bundle_fifo(data);
        } else {
            metadata_bundle_opt = pop_packed_regions_info_for_sequence(data, resolution.sequence_id);
        }
        if (!metadata_bundle_opt.has_value()) {
            if (resolution.used_strict_frame_order) {
                const int remaining =
                        data.output_display_runtime.display_roi_strict_fifo_miss_debug_logs_remaining.fetch_sub(
                                1, std::memory_order_relaxed);
                if (remaining > 0) {
                    sf::debug(
                            output_pipeline,
                            "Display ROI borders: no display_regions_queue bundle available (FIFO strict-order path); "
                            "skipping borders for this frame");
                }
                return GST_PAD_PROBE_OK;
            }
            data.metadata_runtime.record_validation_failure();
            data.request_metadata_contract_fail_stop("display branch missing metadata bundle");
            return GST_PAD_PROBE_OK;
        }

        advance_decoded_branch_roi_sequence_cursor(data.output_display_runtime.display_roi_correlation_next_sequence,
                                                   metadata_bundle_opt->sequence_id);

        const auto &region_info = metadata_bundle_opt->regions;

        if (region_info.cancel_flag) {
            return GST_PAD_PROBE_OK;
        }

        NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
        const auto dimensions_opt = overlay_frame_dimensions(pad, buffer, batch_meta);
        if (!dimensions_opt.has_value()) {
            return GST_PAD_PROBE_OK;
        }

        const auto [width, height] = dimensions_opt.value();
        const auto rectangles =
                get_packed_rectangles_from_pri(region_info, width, height, data.params.debug.draw_rtp_timestamp);
        if (rectangles.empty()) {
            return GST_PAD_PROBE_OK;
        }
        batch_meta = sf::ensure_nvds_batch_meta(buffer, width, height);
        if (batch_meta == nullptr || batch_meta->frame_meta_list == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(batch_meta->frame_meta_list->data);
        if (frame_meta == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        dump_display_border_rectangles(data, static_cast<guint64>(frame_meta->frame_num), pts, width, height,
                                       rectangles);

        nvds_acquire_meta_lock(batch_meta);
        for (size_t rect_index = 0; rect_index < rectangles.size(); ++rect_index) {
            auto *obj_meta = nvds_acquire_obj_meta_from_pool(batch_meta);
            if (obj_meta == nullptr) {
                continue;
            }

            const auto &rect = rectangles[rect_index];
            obj_meta->rect_params.left = static_cast<float>(rect.left);
            obj_meta->rect_params.top = static_cast<float>(rect.top);
            obj_meta->rect_params.width = static_cast<float>(rect.width);
            obj_meta->rect_params.height = static_cast<float>(rect.height);
            obj_meta->rect_params.border_width = 2;
            obj_meta->rect_params.border_color = {0.0F, 1.0F, 0.0F, 1.0F};
            obj_meta->rect_params.has_bg_color = 0;
            obj_meta->confidence = 1.0F;
            obj_meta->class_id = 0;
            obj_meta->object_id = rect_index;
            obj_meta->unique_component_id = 0;

            nvds_add_obj_meta_to_frame(frame_meta, obj_meta, nullptr);
        }
        nvds_release_meta_lock(batch_meta);

        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback for drawing incoming UDP bitrate on display frames.
     * @param pad The pad that received the buffer.
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to global_data.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    [[nodiscard]] GstPadProbeReturn draw_bitrate_probe_gpu(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        auto &data = *static_cast<global_data *>(user_data);
        auto &rt = data.output_display_runtime;

        if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
            return GST_PAD_PROBE_OK;
        }

        // Only draw bitrate if enabled and UDP input is used
        if ((!data.params.debug.draw_bitrate && !data.params.debug.draw_bitrate_large) ||
            data.params.input_type_value != input_type::udp) {
            return GST_PAD_PROBE_OK;
        }

        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (buffer == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        if (!gst_buffer_is_writable(buffer)) {
            buffer = gst_buffer_make_writable(buffer);
            if (buffer == nullptr) {
                return GST_PAD_PROBE_OK;
            }
            GST_PAD_PROBE_INFO_DATA(info) = buffer;
        }

        const auto prepared = prepare_overlay_frame(pad, buffer, data, rt.warned_bitrate_missing_batch_meta,
                                                    rt.warned_bitrate_create_meta_failed);
        if (!prepared.has_value()) {
            return GST_PAD_PROBE_OK;
        }
        const auto [batch_meta, width, height] = prepared.value();

        if (!rt.logged_bitrate_probe) {
            sf::info(output_pipeline, "Display bitrate probe active: %dx%d", width, height);
            rt.logged_bitrate_probe = true;
        }

        // Get current time for throttling
        const GstClockTime current_time = gst_clock_get_time(gst_system_clock_obtain());
        const bool should_update = (rt.bitrate_throttle.last_update_time == GST_CLOCK_TIME_NONE) ||
                                   (current_time - rt.bitrate_throttle.last_update_time >=
                                    bitrate_display_throttle_state::update_interval_ns);

        // Get bitrate string from tracker only if we need to update
        std::string bitrate_str;
        if (should_update) {
            bitrate_str = data.udp_bitrate_tracker.format_bitrate_string();
            if (bitrate_str.empty()) {
                return GST_PAD_PROBE_OK;
            }
            rt.bitrate_throttle.last_bitrate_str = bitrate_str;
            rt.bitrate_throttle.last_update_time = current_time;
        } else {
            // Use cached bitrate string
            bitrate_str = rt.bitrate_throttle.last_bitrate_str;
            if (bitrate_str.empty()) {
                return GST_PAD_PROBE_OK;
            }
        }

        constexpr int font_size_normal = 12;
        constexpr int font_size_large = 24;

        // Draw bitrate text (large red takes precedence if both are enabled)
        if (data.params.debug.draw_bitrate_large) {
            constexpr int padding_x = 32;
            constexpr int padding_y = 32;
            constexpr int safety_margin = 16;
            const int text_width =
                    static_cast<int>(bitrate_str.length()) * (sf::TEXT_CHAR_WIDTH_LARGE + 2) + safety_margin;
            const int start_x = width - text_width - padding_x;
            const int start_y = height - sf::TEXT_CHAR_HEIGHT_LARGE - padding_y;

            if (start_x >= 0 && start_y >= 0 && start_y + sf::TEXT_CHAR_HEIGHT_LARGE <= height) {
                if (!sf::add_text_to_display_meta(batch_meta, bitrate_str.c_str(), start_x, start_y, sf::yuv_color::red,
                                                  font_size_large)) {
                    sf::debug(output_pipeline, "Failed to add large red bitrate '%s' to nvosd meta",
                              bitrate_str.c_str());
                }
            }
        } else if (data.params.debug.draw_bitrate) {
            const auto position_opt = calculate_bitrate_text_position(bitrate_str.length(), width, height);
            if (!position_opt) {
                return GST_PAD_PROBE_OK;
            }

            const auto &[start_x, start_y] = position_opt.value();
            if (!sf::add_text_to_display_meta(batch_meta, bitrate_str.c_str(), start_x, start_y, sf::yuv_color::white,
                                              font_size_normal)) {
                sf::debug(output_pipeline, "Failed to add bitrate '%s' to nvosd meta", bitrate_str.c_str());
            }
        }

        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Pad probe callback for drawing FPS on display frames using NvOSD metadata.
     * @param pad The pad that received the buffer.
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to global_data.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    [[nodiscard]] GstPadProbeReturn draw_fps_probe_gpu(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        auto &data = *static_cast<global_data *>(user_data);
        auto &rt = data.output_display_runtime;

        if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
            return GST_PAD_PROBE_OK;
        }

        if (!data.params.debug.draw_fps) {
            return GST_PAD_PROBE_OK;
        }

        // Get current FPS from pipeline FPS monitor
        const double fps = data.monitoring.fps_monitor.get_current_fps("display");
        const std::string fps_str = format_fps_string(fps);
        if (fps_str.empty()) {
            return GST_PAD_PROBE_OK;
        }

        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (buffer == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        if (!gst_buffer_is_writable(buffer)) {
            buffer = gst_buffer_make_writable(buffer);
            if (buffer == nullptr) {
                return GST_PAD_PROBE_OK;
            }
            GST_PAD_PROBE_INFO_DATA(info) = buffer;
        }

        const auto prepared = prepare_overlay_frame(pad, buffer, data, rt.warned_fps_missing_batch_meta,
                                                    rt.warned_fps_create_meta_failed);
        if (!prepared.has_value()) {
            return GST_PAD_PROBE_OK;
        }
        const auto [batch_meta, width, height] = prepared.value();

        if (!rt.logged_fps_probe) {
            sf::info(output_pipeline, "Display FPS probe active: %dx%d", width, height);
            rt.logged_fps_probe = true;
        }
        const auto position_opt = calculate_fps_text_position(fps_str.length(), width);
        if (!position_opt.has_value()) {
            return GST_PAD_PROBE_OK;
        }

        const auto [start_x, start_y] = position_opt.value();
        constexpr int font_size_normal = 12;
        if (!sf::add_text_to_display_meta(batch_meta, fps_str.c_str(), start_x, start_y, sf::yuv_color::white,
                                          font_size_normal)) {
            sf::debug(output_pipeline, "Failed to add FPS '%s' to nvosd meta", fps_str.c_str());
        }

        (void) height;
        return GST_PAD_PROBE_OK;
    }

    [[nodiscard]] bool link_elements_or_log(GstElement *src, GstElement *dst) {
        if (src == nullptr || dst == nullptr) {
            sf::error(output_pipeline, "Display branch link failed: src=%p dst=%p", static_cast<void *>(src),
                      static_cast<void *>(dst));
            return false;
        }

        if (gst_element_link(src, dst) == TRUE) {
            return true;
        }

        const gchar *src_name = gst_element_get_name(src);
        const gchar *dst_name = gst_element_get_name(dst);
        sf::error(output_pipeline, "Display branch link failed: '%s' -> '%s'", src_name ? src_name : "(null)",
                  dst_name ? dst_name : "(null)");
        return false;
    }

    /**
     * @brief Check if display branch should be enabled.
     * @param data The narrow output branch state view.
     * @return true if display branch should be enabled, false otherwise.
     */
    [[nodiscard]] bool is_display_branch_enabled(const output_branch_facade &data) {
        return data.params.debug.enable_display_branch;
    }

    /**
     * @brief Creates display branch elements.
     * @param data The narrow output branch state view.
     * @param elements Output structure to populate.
     * @return true if successful.
     * @throws std::runtime_error if required elements cannot be created.
     */
    [[nodiscard]] bool create_display_branch_elements(const output_branch_facade &data,
                                                      output_pipeline_elements &elements) {
        elements.queue_display = gst_element_factory_make("queue", "q-disp");
        elements.parser_display = gst_element_factory_make("h265parse", "p-disp");
        elements.caps_display = gst_element_factory_make("capsfilter", "cf-disp");
        if (elements.parser_display) {
            // In random access mode, allow passthrough to avoid dropping frames when they arrive in batches
            // The parser will still insert config-interval SPS/PPS but won't drop frames that need reference frames
            const bool disable_passthrough = data.params.packer_type_value != packer_type::random_access;
            g_object_set(elements.parser_display, "config-interval", 1, "disable-passthrough", disable_passthrough,
                         nullptr);
            sf::info(output_pipeline,
                     "Configured display parser: config-interval=1, disable-passthrough=%s (packer_type=%s)",
                     disable_passthrough ? "true" : "false", packer_type_to_string(data.params.packer_type_value));
        }

        // Display branch H.265 decoder:
        // - x86 nvh265dec: sink often does not accept parser byte-stream caps and/or needs
        //   gst.cuda.context (never supplied) -> d-disp:sink not accepted -> sei_src not-negotiated.
        // - Same remedy as UDP input on x86: avdec_h265 + videoconvert, then existing upload to NVMM.
        // - ARM64: nvv4l2decoder accepts the parser link; only kvazaar (system) path used avdec before—
        //   keep ARM hw decoder when not kvazaar.
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
        const std::string encoder_factory = resolve_encoder_element_factory_name(data.params);
        const encoder_descriptor *const enc_desc = find_encoder_descriptor(encoder_factory);
        const bool display_use_software_decode =
                enc_desc != nullptr && enc_desc->caps_profile == encoder_caps_profile::system;

        if (display_use_software_decode) {
            elements.decoder_display = gst_element_factory_make("avdec_h265", "d-disp");
            if (!elements.decoder_display) {
                sf::error(output_pipeline, "avdec_h265 required for display branch when encoder is kvazaarenc");
                throw std::runtime_error("Display branch software decoder unavailable");
            }
            elements.videoconvert_display_before_nvmm = gst_element_factory_make("videoconvert", "c-disp-sw-before-nv");
            if (!elements.videoconvert_display_before_nvmm) {
                throw std::runtime_error("videoconvert required for display branch with kvazaar encoder");
            }
            sf::info(output_pipeline,
                     "Display branch using avdec_h265 + videoconvert (kvazaar / system encoder on ARM)");
        }

        if (!elements.decoder_display) {
            // RAII_EXEMPT: output branch elements are transferred to GstBin ownership.
            elements.decoder_display = create_h265_decoder("d-disp").release();
        }
        // RAII_EXEMPT: output branch elements are transferred to GstBin ownership.
        elements.converter_display_to_nvmm = create_cpu_to_gpu_upload("c-disp-upload").release();
        elements.caps_display_to_nvmm = gst_element_factory_make("capsfilter", "cf-disp-nvmm");
        elements.streammux_display = gst_element_factory_make("nvstreammux", "mux-disp");
        // RAII_EXEMPT: output branch elements are transferred to GstBin ownership.
        elements.converter_display = create_gpu_video_convert("c-disp").release();
        // RAII_EXEMPT: output branch elements are transferred to GstBin ownership.
        elements.nvosd_display = create_nvosd_element("nvosd-disp").release();
        elements.sink_display = gst_element_factory_make("nv3dsink", "s-disp-actual-sink");
        // Do not use nvegltransform with nv3dsink
#else
        // x86: never nvh265dec on display branch (caps not accepted on sink / CUDA context).
        elements.decoder_display = gst_element_factory_make("avdec_h265", "d-disp");
        if (!elements.decoder_display) {
            sf::error(output_pipeline,
                      "avdec_h265 required for display branch on x86 (nvh265dec needs gst.cuda.context / "
                      "rejects parser caps). Install gst-libav.");
            throw std::runtime_error("Display branch software decoder unavailable");
        }
        elements.videoconvert_display_before_nvmm = gst_element_factory_make("videoconvert", "c-disp-sw-before-nv");
        if (!elements.videoconvert_display_before_nvmm) {
            throw std::runtime_error("videoconvert required for display branch on x86");
        }
        sf::info(output_pipeline,
                 "Display branch using avdec_h265 + videoconvert on x86 (avoids nvh265dec d-disp negotiation failure)");
        // RAII_EXEMPT: output branch elements are transferred to GstBin ownership.
        elements.converter_display_to_nvmm = create_cpu_to_gpu_upload("c-disp-upload").release();
        elements.caps_display_to_nvmm = gst_element_factory_make("capsfilter", "cf-disp-nvmm");
        elements.streammux_display = gst_element_factory_make("nvstreammux", "mux-disp");
        elements.converter_display = gst_element_factory_make("nvvideoconvert", "c-disp");
        // RAII_EXEMPT: output branch elements are transferred to GstBin ownership.
        elements.nvosd_display = create_nvosd_element("nvosd-disp").release();
        elements.sink_display = gst_element_factory_make("nveglglessink", "s-disp-actual-sink");
        if (!elements.sink_display) {
            sf::error(output_pipeline, "nveglglessink unavailable - GPU display not possible");
            throw std::runtime_error("Failed to create nveglglessink - ensure NVIDIA display drivers are installed");
        }
#endif
        if (!elements.sink_display || !elements.caps_display || !elements.converter_display ||
            !elements.decoder_display || !elements.converter_display_to_nvmm || !elements.caps_display_to_nvmm ||
            !elements.streammux_display || !elements.nvosd_display) {
            sf::error(output_pipeline, "Failed to create required display branch elements");
            throw std::runtime_error("Display branch element creation failed - check GPU driver and GStreamer plugins");
        }
        if (elements.caps_display) {
            const char *caps_str = get_display_rgba_caps();
            GstCaps *caps = gst_caps_from_string(caps_str);
            if (caps) {
                g_object_set(elements.caps_display, "caps", caps, nullptr);
                gst_caps_unref(caps);
            } else {
                sf::warning(output_pipeline,
                            "Failed to create caps for display branch; continuing without caps filter");
            }
        }

        if (elements.caps_display_to_nvmm) {
            const char *caps_str = get_display_rgba_caps();
            GstCaps *caps = gst_caps_from_string(caps_str);
            if (caps) {
                g_object_set(elements.caps_display_to_nvmm, "caps", caps, nullptr);
                gst_caps_unref(caps);
            } else {
                sf::warning(output_pipeline,
                            "Failed to create NVMM caps for display streammux; continuing without caps filter");
            }
        }

        if (elements.streammux_display) {
            constexpr int batch_size = 1;
            constexpr int batch_timeout_usec = 40000;
            const int width = data.params.width;
            const int height = data.params.height;
            const int live_source = data.params.input_type_value == input_type::udp ? 1 : 0;
            g_object_set(elements.streammux_display, "batch-size", batch_size, "width", width, "height", height,
                         "batched-push-timeout", batch_timeout_usec, "live-source", live_source, nullptr);
        }

        return true;
    }

    /**
     * @brief Configures display branch elements.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    [[nodiscard]] bool configure_display_branch_elements(const output_branch_facade &data,
                                                         output_pipeline_elements &elements) {
        // Configure display branch queue
        if (elements.queue_display) {
            // Uniform queue size for synchronization across all branches
            const int effective_framerate = data.params.framerate > 0 ? data.params.framerate : 60;
            const int max_buffers = 2 * effective_framerate;
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_display, "max-size-buffers", max_buffers, "max-size-time", 3000 * GST_MSECOND,
                         "leaky", leaky_mode, nullptr);
            sf::debug(output_pipeline,
                      "Configured display branch queue: max-size-buffers=%d, max-size-time=3000ms, leaky=%d "
                      "(packer_type=%s)",
                      max_buffers, leaky_mode, packer_type_to_string(data.params.packer_type_value));
        }

        // Configure display sink
        if (elements.sink_display) {
            // In random access mode, frames arrive in batches from the packer.
            // With sync=true, frames with timestamps in the past relative to the clock get dropped.
            // Use sync=false to display frames immediately as they're decoded, preventing frame drops.
            const bool enable_sync = false; // Always false to prevent dropping batched frames

            // Not all sinks expose all properties (e.g. autovideosink doesn't have qos).
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(elements.sink_display), "sync") != nullptr) {
                g_object_set(elements.sink_display, "sync", enable_sync, nullptr);
            }
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(elements.sink_display), "qos") != nullptr) {
                g_object_set(elements.sink_display, "qos", false, nullptr);
            }
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(elements.sink_display), "enable-last-sample") !=
                nullptr) {
                g_object_set(elements.sink_display, "enable-last-sample", false, nullptr);
            }
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(elements.sink_display), "async") != nullptr) {
                g_object_set(elements.sink_display, "async", false, nullptr);
            }
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
            sf::info(output_pipeline,
                     "Configured display sink: sync=%s, qos=false, enable-last-sample=false (Jetson/ARM64, "
                     "packer_type=%s)",
                     enable_sync ? "true" : "false", packer_type_to_string(data.params.packer_type_value));
#else
            if (elements.converter_display) {
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(elements.converter_display), "nvbuf-memory-type") !=
                    nullptr) {
                    g_object_set(elements.converter_display, "nvbuf-memory-type", 3, nullptr);
                }
            }
            sf::info(output_pipeline,
                     "Configured display sink: sync=%s, qos=false, enable-last-sample=false (packer_type=%s)",
                     enable_sync ? "true" : "false", packer_type_to_string(data.params.packer_type_value));
#endif
        }

        if (elements.nvosd_display) {
            auto *nvosd_class = G_OBJECT_GET_CLASS(elements.nvosd_display);
            if (g_object_class_find_property(nvosd_class, "display-bbox") != nullptr) {
                g_object_set(elements.nvosd_display, "display-bbox", data.params.debug.draw_borders, nullptr);
            }
        }

        return true;
    }

    /**
     * @brief Links display branch elements.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_display_branch(const output_pipeline_elements &elements) {
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
        const bool has_transform = elements.transform_display != nullptr;
#else
        const bool has_transform = false;
#endif

        // Link step-by-step so we can log exactly where linking fails.
        if (!link_elements_or_log(elements.queue_display, elements.parser_display)) {
            return false;
        }
        if (!link_elements_or_log(elements.parser_display, elements.decoder_display)) {
            return false;
        }

        if (elements.videoconvert_display_before_nvmm) {
            if (!link_elements_or_log(elements.decoder_display, elements.videoconvert_display_before_nvmm)) {
                return false;
            }
            if (!link_elements_or_log(elements.videoconvert_display_before_nvmm, elements.converter_display_to_nvmm)) {
                return false;
            }
        } else if (!link_elements_or_log(elements.decoder_display, elements.converter_display_to_nvmm)) {
            return false;
        }
        if (!link_elements_or_log(elements.converter_display_to_nvmm, elements.caps_display_to_nvmm)) {
            return false;
        }

        // Link upload converter -> streammux using request pad.
        GstPad *decoder_src_pad = gst_element_get_static_pad(elements.caps_display_to_nvmm, "src");
        GstPad *streammux_sink_pad = gst_element_request_pad_simple(elements.streammux_display, "sink_0");
        if (!decoder_src_pad || !streammux_sink_pad) {
            if (decoder_src_pad) {
                gst_object_unref(decoder_src_pad);
            }
            if (streammux_sink_pad) {
                gst_object_unref(streammux_sink_pad);
            }
            sf::error(output_pipeline, "Failed to get pads for display streammux linking");
            return false;
        }
        if (gst_pad_link(decoder_src_pad, streammux_sink_pad) != GST_PAD_LINK_OK) {
            gst_object_unref(decoder_src_pad);
            gst_object_unref(streammux_sink_pad);
            sf::error(output_pipeline, "Failed to link decoder to display streammux");
            return false;
        }
        gst_object_unref(decoder_src_pad);
        gst_object_unref(streammux_sink_pad);

        if (!link_elements_or_log(elements.streammux_display, elements.converter_display)) {
            return false;
        }
        if (!link_elements_or_log(elements.converter_display, elements.caps_display)) {
            return false;
        }

        // Determine the chain: caps -> transform (if exists) -> nvosd -> sink
        GstElement *last_element = elements.caps_display;

        if (has_transform) {
            if (!link_elements_or_log(last_element, elements.transform_display)) {
                return false;
            }
            last_element = elements.transform_display;
        }

        if (!link_elements_or_log(last_element, elements.nvosd_display)) {
            return false;
        }
        last_element = elements.nvosd_display;

        return link_elements_or_log(last_element, elements.sink_display);
    }

    /**
     * @brief Links tee to display branch.
     * @param tee The tee element.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_tee_to_display_branch(GstElement *tee, const output_pipeline_elements &elements) {
        return link_tee_to_branch(tee, elements.queue_display);
    }

    /**
     * @brief Adds display branch elements to pipeline.
     * @param data The narrow output branch state view.
     * @param elements The pipeline elements.
     */
    void add_display_branch_to_pipeline(const output_branch_facade &data, const output_pipeline_elements &elements) {
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
        const bool has_transform = elements.transform_display != nullptr;
        if (has_transform) {
            gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_display, elements.parser_display,
                             elements.decoder_display, elements.converter_display_to_nvmm,
                             elements.caps_display_to_nvmm, elements.streammux_display, elements.converter_display,
                             elements.caps_display, elements.transform_display, elements.nvosd_display,
                             elements.sink_display, nullptr);
            if (elements.videoconvert_display_before_nvmm) {
                gst_bin_add(GST_BIN(data.output_pipeline.get()), elements.videoconvert_display_before_nvmm);
            }
        } else {
            gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_display, elements.parser_display,
                             elements.decoder_display, elements.converter_display_to_nvmm,
                             elements.caps_display_to_nvmm, elements.streammux_display, elements.converter_display,
                             elements.caps_display, elements.nvosd_display, elements.sink_display, nullptr);
            if (elements.videoconvert_display_before_nvmm) {
                gst_bin_add(GST_BIN(data.output_pipeline.get()), elements.videoconvert_display_before_nvmm);
            }
        }
#else
        gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_display, elements.parser_display,
                         elements.decoder_display, elements.converter_display_to_nvmm, elements.caps_display_to_nvmm,
                         elements.streammux_display, elements.converter_display, elements.caps_display,
                         elements.nvosd_display, elements.sink_display, nullptr);
        if (elements.videoconvert_display_before_nvmm) {
            gst_bin_add(GST_BIN(data.output_pipeline.get()), elements.videoconvert_display_before_nvmm);
        }
#endif
    }
} // namespace

output_branch_functions create_display_branch_functions() {
    return output_branch_functions{
            .is_enabled = [](const output_branch_facade &data) { return is_display_branch_enabled(data); },
            .create_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_display_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return create_display_branch_elements(data, elements);
                    },
            .configure_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_display_branch_enabled(data) || !elements.sink_display) {
                            return true; // Not enabled or elements not created, but not an error
                        }
                        return configure_display_branch_elements(data, elements);
                    },
            .link_branch =
                    [](const output_pipeline_elements &elements) {
                        if (!elements.sink_display) {
                            return true; // Not created, but not an error
                        }
                        return link_display_branch(elements);
                    },
            .link_to_tee =
                    [](GstElement *tee, const output_pipeline_elements &elements) {
                        if (!elements.sink_display) {
                            return true; // Not created, but not an error
                        }
                        return link_tee_to_display_branch(tee, elements);
                    },
            .add_to_pipeline =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (is_display_branch_enabled(data) && elements.sink_display) {
                            add_display_branch_to_pipeline(data, elements);
                        }
                    },
            .get_branch_name = []() { return "display"; },
            .setup_signals =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (!is_display_branch_enabled(data)) {
                            return true;
                        }

                        bool success = true;
                        const auto overlay_flags = get_display_overlay_flags(data);
                        const bool overlays_enabled = overlay_flags.draw_fps || overlay_flags.draw_bitrate ||
                                                      overlay_flags.draw_bitrate_large;
                        const bool borders_enabled = data.params.debug.draw_borders;
                        if (!overlays_enabled && !borders_enabled) {
                            return true;
                        }

                        GstElement *probe_element =
                                elements.streammux_display ? elements.streammux_display : elements.nvosd_display;
                        const char *pad_name = elements.streammux_display ? "src" : "sink";
                        if (!probe_element) {
                            sf::warning(output_pipeline, "Display overlays requested but probe element is missing");
                            return false;
                        }

                        GstPad *pad = gst_element_get_static_pad(probe_element, pad_name);
                        if (!pad) {
                            sf::warning(output_pipeline, "Failed to get pad '%s' for display probes", pad_name);
                            return false;
                        }

                        if (borders_enabled) {
                            const gulong probe_id =
                                    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, add_roi_borders_from_pri_probe,
                                                      &data.instance(), nullptr);
                            if (probe_id == 0) {
                                sf::warning(output_pipeline, "Failed to attach display border probe (%s:%s)",
                                            elements.streammux_display ? "streammux" : "nvosd", pad_name);
                                success = false;
                            } else {
                                sf::info(output_pipeline, "Attached display border probe (%s:%s)",
                                         elements.streammux_display ? "streammux" : "nvosd", pad_name);
                            }
                        }

                        if (overlay_flags.draw_fps) {
                            const gulong probe_id = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                                                                      draw_fps_probe_gpu, &data.instance(), nullptr);
                            if (probe_id == 0) {
                                sf::warning(output_pipeline, "Failed to attach display FPS probe (%s:%s)",
                                            elements.streammux_display ? "streammux" : "nvosd", pad_name);
                                success = false;
                            } else {
                                sf::info(output_pipeline, "Attached display FPS probe (%s:%s)",
                                         elements.streammux_display ? "streammux" : "nvosd", pad_name);
                            }
                        }

                        if (overlay_flags.draw_bitrate || overlay_flags.draw_bitrate_large) {
                            const gulong probe_id = gst_pad_add_probe(
                                    pad, GST_PAD_PROBE_TYPE_BUFFER, draw_bitrate_probe_gpu, &data.instance(), nullptr);
                            if (probe_id == 0) {
                                sf::warning(output_pipeline, "Failed to attach display bitrate probe (%s:%s)",
                                            elements.streammux_display ? "streammux" : "nvosd", pad_name);
                                success = false;
                            } else {
                                sf::info(output_pipeline, "Attached display bitrate probe (%s:%s)",
                                         elements.streammux_display ? "streammux" : "nvosd", pad_name);
                            }
                        }

                        gst_object_unref(pad);
                        return success;
                    }};
}

output_branch_context create_display_branch_context() {
    return output_branch_context{.functions = create_display_branch_functions()};
}
