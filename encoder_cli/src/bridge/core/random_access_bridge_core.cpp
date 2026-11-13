// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "random_access_bridge_core.h"

#include <chrono>
#include <cstring>
#include <glib.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <map>
#include <ranges>
#include <unordered_map>

#include "../../../../libvcai/include/area_copy.h"
#include "../../../../libvcai/include/background_blur.h"
#include "../../../../libvcai/include/logging.h"
#include "../../bridge/app/encoder_app_adapter.h"
#include "../../bridge/context/encoder_bridge_context.h"
#include "../../bridge/orchestration/encoded_bridge_orchestrator.h"
#include "../../global_data.h"
#include "../../gst/input_types/frame_path_meta.h"
#include "../../gst/input_types/rtp_timestamp_overlay.h"
#include "../../input_analyzer/input_analyzer.h"
#include "../../processing/frame_processing_utils.h"
#include "../../processing/roi_detection_utils.h"
#include "../../roi/roi_manager.h"

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    void maybe_apply_luma_downscaling(global_data &data, sf::unique_gst_buffer_ptr &buffer,
                                      const frame_properties &props, const char *context_label,
                                      const int frame_number) {
        if (!data.params.enable_luma_adaptation) {
            return;
        }

        const luma_factors_for_frame factors = get_luma_factors_for_frame(data, buffer.get(), props);
        if (!apply_luma_scaling_to_buffer(buffer, props, factors.downscale_factor)) {
            sf::warning(roi_detection_processing,
                        "Frame %d: Failed to apply luma downscaling in %s path; keeping original luma values",
                        frame_number, context_label);
        }
    }

    [[nodiscard]] std::unordered_map<std::string, sf::rectangle>
    build_object_id_to_packed_mapping(const random_access_packer::complete_period &period) {
        std::unordered_map<std::string, sf::rectangle> object_id_to_packed;

        if (!period.original_to_packed_mapping.empty() &&
            period.original_to_packed_mapping.size() == period.object_ids.size()) {
            for (size_t i = 0; i < period.object_ids.size(); ++i) {
                const size_t packed_idx = period.original_to_packed_mapping[i];
                if (packed_idx < period.processed_rois.size()) {
                    object_id_to_packed[period.object_ids[i]] = period.processed_rois[packed_idx];
                } else {
                    sf::warning(roi_detection_processing,
                                "Invalid packed_idx %zu for object_id '%s' (processed_rois size: %zu)", packed_idx,
                                period.object_ids[i].c_str(), period.processed_rois.size());
                }
            }
        } else if (period.processed_rois.size() == period.object_ids.size()) {
            for (size_t i = 0; i < period.object_ids.size(); ++i) {
                object_id_to_packed[period.object_ids[i]] = period.processed_rois[i];
            }
        } else {
            sf::error(roi_detection_processing,
                      "Cannot build mapping: object_ids size (%zu) != processed_rois size (%zu), mapping size: %zu",
                      period.object_ids.size(), period.processed_rois.size(), period.original_to_packed_mapping.size());
        }

        return object_id_to_packed;
    }

    [[nodiscard]] std::unordered_map<std::string, sf::rectangle>
    build_accumulated_source_rois_from_period_frames(const random_access_packer::complete_period &period) {
        std::unordered_map<std::string, sf::rectangle> accumulated_source_rois;
        for (const auto &buffered_frame: period.frames) {
            for (const auto &[source_rect, object_id]: buffered_frame.rectangles_with_ids) {
                if (accumulated_source_rois.contains(object_id)) {
                    accumulated_source_rois[object_id].expand_with(source_rect);
                } else {
                    accumulated_source_rois[object_id] = source_rect;
                }
            }
        }
        return accumulated_source_rois;
    }

    [[nodiscard]] GstFlowReturn push_to_encoding(global_data &data, sf::unique_gst_buffer_ptr buffer,
                                                 const frame_contract_info &frame_contract) {
        if (!attach_frame_contract_metadata_to_buffer(buffer.get(), frame_contract.path, frame_contract.sequence_id)) {
            data.metadata_runtime.record_validation_failure();
            data.request_metadata_contract_fail_stop("random_access failed to attach output frame contract metadata");
            return GST_FLOW_ERROR;
        }
        data.push_pending_encoded_contract_state(
                frame_contract.sequence_id,
                {.enabled = false, .downscale_factor = 1.0, .upscale_multiplier = std::nullopt});

        const encoder_app_adapter app_adapter(data);
        auto bridge_context = encoded_bridge_context{.encoding_src = data.encoded_ports.encoding_src,
                                                     .sei_src = data.encoded_ports.sei_src,
                                                     .runtime = &data.egress_bridge_runtime};
        const encoded_bridge_push_options options{
                .update_output_counter = false,
                .update_sei_injection_counter = false,
                .update_encoder_submission_counter = true,
                .rollback_pending_luma_on_failure = true,
        };
        const auto result = encoded_bridge_push_owned_buffer_via_orchestrator(
                bridge_context, app_adapter, encoded_bridge_target::encoding_src, std::move(buffer), options);
        return result.flow_return;
    }

    void push_sentinel_for_sync(global_data &data, const uint64_t sequence_id) {
        data.packed_regions_queue.push(
                {.sequence_id = sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
        data.metadata_runtime.record_bundle_enqueued();
        if (data.params.debug.draw_borders) {
            data.display_regions_queue.push(
                    {.sequence_id = sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
            data.metadata_runtime.record_bundle_enqueued();
        }
        if (data.params.debug.save_frames) {
            data.debug_regions_queue.push(
                    {.sequence_id = sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
            data.metadata_runtime.record_bundle_enqueued();
        }
    }

    void handle_frame_missing_sample(global_data &data, const int frame_number) {
        data.metadata_runtime.record_validation_failure();
        data.request_metadata_contract_fail_stop("random_access frame missing sample");
        sf::error(roi_detection_processing, "Frame %d: missing sample, strict metadata contract requires fail-stop",
                  frame_number);
    }

    void handle_frame_missing_properties(global_data &data, const int frame_number) {
        data.metadata_runtime.record_validation_failure();
        data.request_metadata_contract_fail_stop("random_access frame missing properties");
        sf::error(roi_detection_processing,
                  "Could not get frame properties for buffered frame %d, strict metadata contract requires fail-stop",
                  frame_number);
    }

    void handle_frame_no_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &buffered_buffer,
                              const int frame_number, const frame_properties &props,
                              const frame_contract_info &frame_contract) {
        sf::debug(roi_detection_processing, "Frame %d has no ROIs, pushing empty initialized frame", frame_number);
        push_sentinel_for_sync(data, frame_contract.sequence_id);
        auto empty_buffer = create_output_buffer(buffered_buffer);
        if (empty_buffer) {
            (void) attach_frame_contract_metadata_to_buffer(empty_buffer.get(), frame_contract.path,
                                                            frame_contract.sequence_id);
            maybe_apply_luma_downscaling(data, empty_buffer, props, "no-roi", frame_number);
            if (data.params.debug.draw_rtp_timestamp && !draw_rtp_timestamp_on_buffer(empty_buffer.get(), props)) {
                sf::warning(roi_detection_processing, "Failed to draw RTP timestamp on empty frame %d", frame_number);
            }
            const GstFlowReturn flow_ret = push_to_encoding(data, std::move(empty_buffer), frame_contract);
            if (flow_ret != GST_FLOW_OK && flow_ret != GST_FLOW_FLUSHING) {
                sf::warning(roi_detection_processing, "Backpressure detected pushing empty frame %d (flow: %s)",
                            frame_number, gst_flow_get_name(flow_ret));
                g_usleep(1000); // 1ms yield under GstTask / GLib scheduling
            }
        } else {
            sf::error(roi_detection_processing, "Failed to create empty buffer for frame %d, skipping", frame_number);
        }
    }

    [[nodiscard]] std::map<std::pair<int, int>, std::tuple<sf::rectangle, sf::rectangle, std::string>>
    map_rois_to_packed_positions(const random_access_packer::buffered_frame &buffered,
                                 const std::unordered_map<std::string, sf::rectangle> &object_id_to_packed,
                                 const random_access_packer::complete_period &period, const int max_x, const int max_y,
                                 const std::unordered_map<std::string, sf::rectangle> *source_rois_by_object_id) {
        std::map<std::pair<int, int>, std::tuple<sf::rectangle, sf::rectangle, std::string>> unique_packed_positions;

        for (size_t i = 0; i < period.object_ids.size(); ++i) {
            const std::string &object_id = period.object_ids[i];
            sf::rectangle packed_pos;
            bool found = false;

            if (object_id_to_packed.contains(object_id)) {
                packed_pos = object_id_to_packed.at(object_id);
                found = true;
            } else {
                sf::warning(roi_detection_processing,
                            "Frame %d: No packed position found for object_id %s (available IDs: %zu)",
                            buffered.frame_number, object_id.c_str(), object_id_to_packed.size());
                continue;
            }

            sf::rectangle accumulated_roi;
            bool original_found = false;
            if (source_rois_by_object_id && source_rois_by_object_id->contains(object_id)) {
                accumulated_roi = source_rois_by_object_id->at(object_id);
                original_found = true;
            } else if (i < period.original_rois.size()) {
                accumulated_roi = period.original_rois[i];
                original_found = true;
            } else {
                sf::warning(roi_detection_processing,
                            "Frame %d: No original ROI found for object_id %s at index %zu (original_rois size: %zu)",
                            buffered.frame_number, object_id.c_str(), i, period.original_rois.size());
                continue;
            }

            if (found && original_found) {
                const sf::rectangle &source_roi = accumulated_roi;
                const int clamped_x = std::max(0, std::min(packed_pos.left, max_x - accumulated_roi.width));
                const int clamped_y = std::max(0, std::min(packed_pos.top, max_y - accumulated_roi.height));
                const int clamped_w = std::min(accumulated_roi.width, max_x - clamped_x);
                const int clamped_h = std::min(accumulated_roi.height, max_y - clamped_y);

                if (clamped_w > 0 && clamped_h > 0) {
                    const sf::rectangle packed_rect(clamped_x, clamped_y, clamped_w, clamped_h);
                    const auto key = std::make_pair(packed_rect.left, packed_rect.top);
                    if (!unique_packed_positions.contains(key) ||
                        (source_roi.width * source_roi.height >
                         std::get<1>(unique_packed_positions[key]).width *
                                 std::get<1>(unique_packed_positions[key]).height)) {
                        unique_packed_positions[key] = std::make_tuple(packed_rect, source_roi, object_id);
                    }
                } else {
                    sf::warning(roi_detection_processing,
                                "Frame %d: Packed ROI for object_id %s would be out of bounds, skipping",
                                buffered.frame_number, object_id.c_str());
                }
            }
        }

        return unique_packed_positions;
    }

    void process_buffered_frame(global_data &data, const random_access_packer::buffered_frame &buffered,
                                const random_access_packer::complete_period &period,
                                const std::unordered_map<std::string, sf::rectangle> &object_id_to_packed,
                                const frame_properties &props) {
        auto *buffered_buffer = gst_sample_get_buffer(buffered.sample.get());
        const auto frame_contract = get_frame_contract_from_buffer(buffered_buffer);
        if (!frame_contract.has_value()) {
            data.metadata_runtime.record_validation_failure();
            data.request_metadata_contract_fail_stop("random_access buffered frame missing contract metadata");
            sf::error(roi_detection_processing, "Frame %d missing contract metadata in random-access path",
                      buffered.frame_number);
            return;
        }

        const bool placement_only_margin =
                data.params.roi_fixed_margin_px > 0 && data.params.roi_margin_mode_value == roi_margin_mode::placement;
        std::unordered_map<std::string, sf::rectangle> source_rois_by_object_id;
        const std::unordered_map<std::string, sf::rectangle> *source_rois_by_object_id_ptr = nullptr;
        if (placement_only_margin) {
            source_rois_by_object_id = build_accumulated_source_rois_from_period_frames(period);
            source_rois_by_object_id_ptr = &source_rois_by_object_id;
        }

        const auto unique_packed_positions =
                map_rois_to_packed_positions(buffered, object_id_to_packed, period, data.params.width,
                                             data.params.height, source_rois_by_object_id_ptr);

        if (unique_packed_positions.empty()) {
            const sf::unique_gst_buffer_no_delete_ptr buffer_wrapper(buffered_buffer);
            handle_frame_no_rois(data, buffer_wrapper, buffered.frame_number, props, *frame_contract);
            return;
        }

        std::vector<sf::rectangle> packed_rectangles;
        std::vector<sf::rectangle> input_rectangles_for_packing;
        packed_rectangles.reserve(unique_packed_positions.size());
        input_rectangles_for_packing.reserve(unique_packed_positions.size());

        for (const auto &entry: unique_packed_positions | std::views::values) {
            packed_rectangles.push_back(std::get<0>(entry));
            input_rectangles_for_packing.push_back(std::get<1>(entry));
        }

        if (data.params.debug.draw_rtp_timestamp) {
            const sf::rectangle ts_roi = create_timestamp_roi_rectangle(data.params.width, data.params.height);
            if (ts_roi.is_valid()) {
                input_rectangles_for_packing.push_back(ts_roi);
                packed_rectangles.push_back(ts_roi);
            }
        }

        if (!packed_rectangles.empty() && packed_rectangles.size() == input_rectangles_for_packing.size()) {
            sf::unique_gst_buffer_ptr output_buffer;

            if (data.params.is_background_blur_effective()) {
                const sf::unique_gst_buffer_no_delete_ptr buffer_wrapper(buffered_buffer, sf::no_delete_deleter{});
                output_buffer = create_output_buffer(buffer_wrapper);
                if (!output_buffer) {
                    sf::error(roi_detection_processing, "[Frame %d] Failed to create output buffer for background blur",
                              buffered.frame_number);
                    return;
                }

                if (!copy_video_frame_data(buffer_wrapper, output_buffer, props, &data.memory_runtime)) {
                    sf::error(roi_detection_processing,
                              "[Frame %d] Failed to copy video frame data for background blur", buffered.frame_number);
                    return;
                }

                const sf::unique_gst_buffer_no_delete_ptr output_wrapper(output_buffer.get());
                const sf::video_format blur_format_enum =
                        props.format ? sf::string_to_video_format(props.format) : sf::video_format::nv12;
                if (!sf::apply_background_blur(output_wrapper, input_rectangles_for_packing, data.params.sigma,
                                               props.width, props.height, blur_format_enum, data.params.kernel_size)) {
                    sf::error(roi_detection_processing, "[Frame %d] Failed to apply background blur",
                              buffered.frame_number);
                    return;
                }

                push_sentinel_for_sync(data, frame_contract->sequence_id);

                const GstClockTime pts = GST_BUFFER_PTS(buffered_buffer);
                GstClockTime dts = GST_BUFFER_DTS(buffered_buffer);
                const GstClockTime duration = GST_BUFFER_DURATION(buffered_buffer);

                const int framerate = data.params.framerate > 0 ? data.params.framerate : 30;
                const auto expected_frame_duration = static_cast<GstClockTime>(GST_SECOND / framerate);
                const GstClockTime validation_duration =
                        (duration != GST_CLOCK_TIME_NONE && duration < GST_CLOCK_TIME_NONE / 2)
                                ? duration
                                : expected_frame_duration;

                if (dts >= GST_CLOCK_TIME_NONE / 2 || dts > pts + validation_duration * 10) {
                    sf::warning(roi_detection_processing,
                                "Frame %d: DTS still invalid in background_blur path, fixing (DTS=%" GST_TIME_FORMAT
                                ", PTS=%" GST_TIME_FORMAT ")",
                                buffered.frame_number, GST_TIME_ARGS(dts), GST_TIME_ARGS(pts));
                    dts = pts;
                }

                const guint64 offset = GST_BUFFER_OFFSET(buffered_buffer);
                const guint64 offset_end = GST_BUFFER_OFFSET_END(buffered_buffer);
                const guint flags = GST_BUFFER_FLAGS(buffered_buffer);

                GST_BUFFER_PTS(output_buffer.get()) = pts;
                GST_BUFFER_DTS(output_buffer.get()) = dts;
                GST_BUFFER_DURATION(output_buffer.get()) = duration;
                GST_BUFFER_OFFSET(output_buffer.get()) = offset;
                GST_BUFFER_OFFSET_END(output_buffer.get()) = offset_end;
                GST_BUFFER_FLAGS(output_buffer.get()) = flags;

                if (data.params.debug.draw_rtp_timestamp && !draw_rtp_timestamp_on_buffer(output_buffer.get(), props)) {
                    sf::warning(roi_detection_processing, "Failed to draw RTP timestamp on frame %d (ROI copy path)",
                                buffered.frame_number);
                }

            } else {
                const sf::unique_gst_buffer_no_delete_ptr buffer_wrapper(buffered_buffer, sf::no_delete_deleter{});
                output_buffer = create_output_buffer(buffer_wrapper);
                if (!output_buffer) {
                    sf::error(roi_detection_processing,
                              "Could not create output buffer for frame %d, pushing sentinel and empty frame",
                              buffered.frame_number);
                    push_sentinel_for_sync(data, frame_contract->sequence_id);
                    const sf::video_format empty_format_enum =
                            props.format ? sf::string_to_video_format(props.format) : sf::video_format::i420;
                    auto empty_buffer = sf::create_empty_buffer(
                            props.width, props.height, empty_format_enum,
                            data.params.background_color == 0
                                    ? sf::background_color::green
                                    : static_cast<sf::background_color>(data.params.background_color));
                    if (empty_buffer) {
                        (void) attach_frame_contract_metadata_to_buffer(empty_buffer.get(), frame_contract->path,
                                                                        frame_contract->sequence_id);
                        const GstFlowReturn flow_ret = push_to_encoding(data, std::move(empty_buffer), *frame_contract);
                        if (flow_ret != GST_FLOW_OK && flow_ret != GST_FLOW_FLUSHING) {
                            sf::warning(roi_detection_processing,
                                        "Backpressure detected pushing empty frame %d (flow: %s)",
                                        buffered.frame_number, gst_flow_get_name(flow_ret));
                            g_usleep(1000); // 1ms yield under GstTask / GLib scheduling
                        }
                    } else {
                        sf::error(roi_detection_processing, "Failed to create empty buffer for frame %d",
                                  buffered.frame_number);
                    }
                    return;
                }

                const GstClockTime pts = GST_BUFFER_PTS(buffered_buffer);
                const GstClockTime dts = GST_BUFFER_DTS(buffered_buffer);
                const GstClockTime duration = GST_BUFFER_DURATION(buffered_buffer);

                if (pts != GST_CLOCK_TIME_NONE && pts < GST_CLOCK_TIME_NONE / 2) {
                    GST_BUFFER_PTS(output_buffer.get()) = pts;
                } else {
                    sf::warning(roi_detection_processing, "Frame %d: Invalid PTS=%" GST_TIME_FORMAT ", using 0",
                                buffered.frame_number, GST_TIME_ARGS(pts));
                    GST_BUFFER_PTS(output_buffer.get()) = 0;
                }

                if (dts != GST_CLOCK_TIME_NONE && dts < GST_CLOCK_TIME_NONE / 2) {
                    GST_BUFFER_DTS(output_buffer.get()) = dts;
                } else {
                    GST_BUFFER_DTS(output_buffer.get()) = GST_BUFFER_PTS(output_buffer.get());
                }

                if (duration != GST_CLOCK_TIME_NONE && duration < GST_CLOCK_TIME_NONE / 2) {
                    GST_BUFFER_DURATION(output_buffer.get()) = duration;
                }

                const sf::unique_gst_buffer_no_delete_ptr input_buffer_wrapper(buffered_buffer,
                                                                               sf::no_delete_deleter{});
                create_metadata_and_process_frame(data, input_buffer_wrapper, output_buffer, props,
                                                  input_rectangles_for_packing, packed_rectangles,
                                                  data.params.background_color);

                if (data.params.debug.draw_rtp_timestamp && !draw_rtp_timestamp_on_buffer(output_buffer.get(), props)) {
                    sf::warning(roi_detection_processing,
                                "Failed to draw RTP timestamp on frame %d (normal packing path)",
                                buffered.frame_number);
                }
            }

            const GstClockTime output_pts = GST_BUFFER_PTS(output_buffer.get());
            const GstClockTime output_dts = GST_BUFFER_DTS(output_buffer.get());
            const GstClockTime output_duration = GST_BUFFER_DURATION(output_buffer.get());
            if (buffered.frame_number <= 20 || output_dts >= GST_CLOCK_TIME_NONE / 2) {
                sf::info(roi_detection_processing,
                         "Frame %d: Pushing to encoding pipeline with PTS=%" GST_TIME_FORMAT ", DTS=%" GST_TIME_FORMAT
                         ", duration=%" GST_TIME_FORMAT,
                         buffered.frame_number, GST_TIME_ARGS(output_pts), GST_TIME_ARGS(output_dts),
                         GST_TIME_ARGS(output_duration));
            }
            maybe_apply_luma_downscaling(data, output_buffer, props, "random-access-output", buffered.frame_number);
            (void) attach_frame_contract_metadata_to_buffer(output_buffer.get(), frame_contract->path,
                                                            frame_contract->sequence_id);
            const GstFlowReturn flow_ret = push_to_encoding(data, std::move(output_buffer), *frame_contract);
            if (flow_ret != GST_FLOW_OK && flow_ret != GST_FLOW_FLUSHING) {
                sf::warning(roi_detection_processing,
                            "Backpressure detected pushing frame %d (flow: %s), encoder may be overwhelmed",
                            buffered.frame_number, gst_flow_get_name(flow_ret));
                g_usleep(1000); // 1ms yield under GstTask / GLib scheduling
            }
        } else {
            sf::warning(roi_detection_processing,
                        "Failed to map packed rectangles for frame %d (packed: %zu, input: %zu)", buffered.frame_number,
                        packed_rectangles.size(), input_rectangles_for_packing.size());
            push_sentinel_for_sync(data, frame_contract->sequence_id);
            const sf::video_format empty_format_enum =
                    props.format ? sf::string_to_video_format(props.format) : sf::video_format::i420;
            auto empty_buffer =
                    sf::create_empty_buffer(props.width, props.height, empty_format_enum,
                                            data.params.background_color == 0
                                                    ? sf::background_color::green
                                                    : static_cast<sf::background_color>(data.params.background_color));
            if (empty_buffer) {
                (void) attach_frame_contract_metadata_to_buffer(empty_buffer.get(), frame_contract->path,
                                                                frame_contract->sequence_id);
                const GstFlowReturn flow_ret = push_to_encoding(data, std::move(empty_buffer), *frame_contract);
                if (flow_ret != GST_FLOW_OK && flow_ret != GST_FLOW_FLUSHING) {
                    sf::warning(roi_detection_processing, "Backpressure detected pushing empty frame %d (flow: %s)",
                                buffered.frame_number, gst_flow_get_name(flow_ret));
                    g_usleep(1000); // 1ms yield under GstTask / GLib scheduling
                }
            } else {
                sf::error(roi_detection_processing, "Could not create black buffer for frame %d in fallback path",
                          buffered.frame_number);
            }
        }
    }

    [[nodiscard]] std::tuple<GstClockTime, GstClockTime, GstClockTime>
    validate_and_fix_timestamps(GstBuffer *buffered_buffer, const int frame_number, const GstClockTime frame_duration,
                                GstClockTime &next_pts) {
        GstClockTime pts = GST_BUFFER_PTS(buffered_buffer);
        GstClockTime dts = GST_BUFFER_DTS(buffered_buffer);
        GstClockTime duration = GST_BUFFER_DURATION(buffered_buffer);
        const GstClockTime original_pts = pts;
        const GstClockTime original_dts = dts;

        if (frame_number <= 20 || dts >= GST_CLOCK_TIME_NONE / 2) {
            sf::info(roi_detection_processing,
                     "Frame %d: Original timestamps - PTS=%" GST_TIME_FORMAT ", DTS=%" GST_TIME_FORMAT
                     ", duration=%" GST_TIME_FORMAT,
                     frame_number, GST_TIME_ARGS(pts), GST_TIME_ARGS(dts), GST_TIME_ARGS(duration));
        }

        if (pts == GST_CLOCK_TIME_NONE || (next_pts != GST_CLOCK_TIME_NONE && pts < next_pts)) {
            if (next_pts == GST_CLOCK_TIME_NONE) {
                next_pts = 0;
            }
            pts = next_pts;
            sf::info(roi_detection_processing,
                     "Frame %d: Calculated PTS=%" GST_TIME_FORMAT " (original was %" GST_TIME_FORMAT ")", frame_number,
                     GST_TIME_ARGS(pts),
                     original_pts == GST_CLOCK_TIME_NONE ? GST_TIME_ARGS(0) : GST_TIME_ARGS(original_pts));
        }

        if (duration == GST_CLOCK_TIME_NONE) {
            duration = frame_duration;
            sf::info(roi_detection_processing, "Frame %d: Set duration=%" GST_TIME_FORMAT " (was missing)",
                     frame_number, GST_TIME_ARGS(duration));
        }

        if (dts == GST_CLOCK_TIME_NONE || dts >= GST_CLOCK_TIME_NONE / 2 || dts > pts + frame_duration * 10) {
            const GstClockTime old_dts = dts;
            dts = pts;
            sf::info(roi_detection_processing,
                     "Frame %d: Invalid DTS=%" GST_TIME_FORMAT ", using PTS=%" GST_TIME_FORMAT " as DTS", frame_number,
                     GST_TIME_ARGS(old_dts), GST_TIME_ARGS(pts));
        }

        GST_BUFFER_PTS(buffered_buffer) = pts;
        GST_BUFFER_DTS(buffered_buffer) = dts;
        GST_BUFFER_DURATION(buffered_buffer) = duration;

        if (frame_number <= 20 || original_dts >= GST_CLOCK_TIME_NONE / 2) {
            sf::info(roi_detection_processing,
                     "Frame %d: Final timestamps after validation - PTS=%" GST_TIME_FORMAT ", DTS=%" GST_TIME_FORMAT
                     ", duration=%" GST_TIME_FORMAT,
                     frame_number, GST_TIME_ARGS(pts), GST_TIME_ARGS(dts), GST_TIME_ARGS(duration));
        }

        next_pts = pts + duration;
        return {pts, dts, duration};
    }

    void pace_frame_push(const int frame_number, const GstClockTime duration,
                         std::chrono::steady_clock::time_point &last_push_time) {
        const auto now = std::chrono::steady_clock::now();
        const auto time_since_last_push = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_push_time);
        const auto frame_duration_ns = std::chrono::nanoseconds(duration);

        if (time_since_last_push < frame_duration_ns) {
            const auto sleep_duration = frame_duration_ns - time_since_last_push;
            const auto sleep_us = std::chrono::duration_cast<std::chrono::microseconds>(sleep_duration).count();
            if (sleep_us > 0) {
                // Cap slice to 1s to keep g_usleep arguments bounded under GstTask.
                const gint64 capped = std::min(sleep_us, static_cast<gint64>(1000000));
                g_usleep(static_cast<gulong>(capped));
            }
            sf::debug(roi_detection_processing, "Frame %d: Paced push, waited %lld ns (frame_duration=%lld ns)",
                      frame_number, sleep_duration.count(), frame_duration_ns.count());
        }
        last_push_time = std::chrono::steady_clock::now();
    }

    void process_frame_in_period(global_data &data, const random_access_packer::buffered_frame &buffered,
                                 const random_access_packer::complete_period &period,
                                 const std::unordered_map<std::string, sf::rectangle> &object_id_to_packed,
                                 const GstClockTime frame_duration, GstClockTime &next_pts,
                                 std::chrono::steady_clock::time_point &last_push_time, int &expected_frame) {
        if (buffered.frame_number != expected_frame) {
            sf::warning(roi_detection_processing, "Frame number mismatch: expected %d, got %d in period %d",
                        expected_frame, buffered.frame_number, period.period_number);
        }
        expected_frame++;

        if (!buffered.sample) {
            sf::error(roi_detection_processing, "Frame %d has null sample in period %d - this should not happen!",
                      buffered.frame_number, period.period_number);
            handle_frame_missing_sample(data, buffered.frame_number);
            return;
        }

        const auto props_opt = get_frame_properties(buffered.sample);
        if (!props_opt) {
            handle_frame_missing_properties(data, buffered.frame_number);
            return;
        }

        auto *buffered_buffer = gst_sample_get_buffer(buffered.sample.get());
        const auto [pts, dts, duration] =
                validate_and_fix_timestamps(buffered_buffer, buffered.frame_number, frame_duration, next_pts);
        (void) pts;
        (void) dts;
        (void) duration;

        pace_frame_push(buffered.frame_number, GST_BUFFER_DURATION(buffered_buffer), last_push_time);
        process_buffered_frame(data, buffered, period, object_id_to_packed, *props_opt);
    }

    void process_period_impl(global_data &data, const random_access_packer::complete_period &period,
                             const GstClockTime frame_duration, GstClockTime &next_pts,
                             std::chrono::steady_clock::time_point &last_push_time) {
        const auto object_id_to_packed = build_object_id_to_packed_mapping(period);
        sf::info(roi_detection_processing, "Processing period %d (start_frame=%d) with %zu frames",
                 period.period_number, period.period_start_frame, period.frames.size());

        int expected_frame = period.period_start_frame;
        for (const auto &buffered: period.frames) {
            process_frame_in_period(data, buffered, period, object_id_to_packed, frame_duration, next_pts,
                                    last_push_time, expected_frame);
        }
    }

    random_access_bridge_core::step_result cleanup_impl(global_data &data, random_access_packer *packer) {
        if (packer == nullptr) {
            return {.status = random_access_bridge_core::step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
        }

        packer->close();
        data.close_metadata_queues_once();
        auto bridge_context = encoded_bridge_context{.encoding_src = data.encoded_ports.encoding_src,
                                                     .sei_src = data.encoded_ports.sei_src,
                                                     .runtime = &data.egress_bridge_runtime};
        const encoder_app_adapter app_adapter(data);
        const auto eos_result = encoded_bridge_signal_eos_via_orchestrator(bridge_context, app_adapter,
                                                                           encoded_bridge_target::encoding_src);
        if (eos_result.status == encoded_bridge_step_status::fatal_stop) {
            sf::warning(roi_detection_processing, "Failed to send EOS to encoding appsrc during cleanup: %s",
                        gst_flow_get_name(eos_result.flow_return));
            return {.status = random_access_bridge_core::step_status::fatal_stop,
                    .flow_return = eos_result.flow_return};
        }

        sf::info(roi_detection_processing, "Frame Processing thread finished");
        return {.status = random_access_bridge_core::step_status::ok, .flow_return = eos_result.flow_return};
    }
} // namespace

random_access_bridge_core::step_result
random_access_bridge_core::process_period(media_session &session, const random_access_packer::complete_period &period,
                                          const GstClockTime frame_duration, GstClockTime &next_pts,
                                          std::chrono::steady_clock::time_point &last_push_time) {
    process_period_impl(session.impl, period, frame_duration, next_pts, last_push_time);
    return {.status = step_status::ok, .flow_return = GST_FLOW_OK};
}

random_access_bridge_core::step_result random_access_bridge_core::cleanup(media_session &session,
                                                                          random_access_packer *packer) {
    return cleanup_impl(session.impl, packer);
}
