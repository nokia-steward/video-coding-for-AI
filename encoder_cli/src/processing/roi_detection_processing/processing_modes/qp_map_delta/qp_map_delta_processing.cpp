// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "qp_map_delta_processing.h"

#include "../../../../../../libvcai/include/logging.h"
#include "../../../../bridge/app/pipeline_app_adapter.h"
#include "../../../../global_data.h"
#include "../../../../gst/input_types/frame_path_meta.h"
#include "../../../../roi/pri_packer_view.h"
#include "../../../../utils/packed_regions_info_utils.h"
#include "../../../frame_processing_utils.h"
#include "../../../roi_detection_packing.h"

#include <algorithm>
#include <cstdint>
#include <vector>

/**
 * @file qp_map_delta_processing.cpp
 * @brief Implementation of QP-map delta processing mode.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    struct ctb_grid_dimensions {
        int columns = 0;
        int rows = 0;
    };

    [[nodiscard]] ctb_grid_dimensions compute_ctb_grid(const int width, const int height, const int ctb_size) {
        return {
                .columns = (width + ctb_size - 1) / ctb_size,
                .rows = (height + ctb_size - 1) / ctb_size,
        };
    }

    [[nodiscard]] std::vector<int8_t> build_qp_delta_map(const int width, const int height, const int ctb_size,
                                                         const int background_delta,
                                                         const std::vector<sf::rectangle> &object_rois) {
        const auto grid = compute_ctb_grid(width, height, ctb_size);
        std::vector<int8_t> qp_delta_map(static_cast<size_t>(grid.columns) * static_cast<size_t>(grid.rows),
                                         static_cast<int8_t>(background_delta));

        for (const auto &roi: object_rois) {
            const int x0 = std::clamp(roi.left, 0, width);
            const int y0 = std::clamp(roi.top, 0, height);
            const int x1 = std::clamp(roi.left + roi.width, 0, width);
            const int y1 = std::clamp(roi.top + roi.height, 0, height);
            if (x1 <= x0 || y1 <= y0) {
                continue;
            }

            const int start_col = x0 / ctb_size;
            const int end_col = (x1 - 1) / ctb_size;
            const int start_row = y0 / ctb_size;
            const int end_row = (y1 - 1) / ctb_size;

            for (int row = start_row; row <= end_row; ++row) {
                for (int column = start_col; column <= end_col; ++column) {
                    const size_t index =
                            static_cast<size_t>(row) * static_cast<size_t>(grid.columns) + static_cast<size_t>(column);
                    if (index < qp_delta_map.size()) {
                        qp_delta_map[index] = 0;
                    }
                }
            }
        }

        return qp_delta_map;
    }

    void ensure_output_buffer_from_input(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                         sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                         memory_policy_runtime *memory_runtime) {
        if (!output_buffer) {
            output_buffer = create_output_buffer(input_buffer, props);
        }
        if (!output_buffer) {
            return;
        }
        (void) copy_video_frame_data(input_buffer, output_buffer, props, memory_runtime);
    }

    void copy_buffer_timing_fields(const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                   const sf::unique_gst_buffer_ptr &output_buffer) {
        GST_BUFFER_PTS(output_buffer.get()) = GST_BUFFER_PTS(input_buffer.get());
        GST_BUFFER_DTS(output_buffer.get()) = GST_BUFFER_DTS(input_buffer.get());
        GST_BUFFER_DURATION(output_buffer.get()) = GST_BUFFER_DURATION(input_buffer.get());
        GST_BUFFER_OFFSET(output_buffer.get()) = GST_BUFFER_OFFSET(input_buffer.get());
        GST_BUFFER_OFFSET_END(output_buffer.get()) = GST_BUFFER_OFFSET_END(input_buffer.get());
        GST_BUFFER_FLAGS(output_buffer.get()) = GST_BUFFER_FLAGS(input_buffer.get());
    }

    void publish_debug_regions(global_data &data, const std::vector<sf::rectangle> &rois, const frame_properties &props,
                               const uint64_t sequence_id) {
        if ((data.params.debug.draw_borders || data.params.debug.save_frames) && !rois.empty()) {
            const creation_params params = {.reconstructed_width = props.width,
                                            .reconstructed_height = props.height,
                                            .log2_unit_size = 0,
                                            .persistence_flag = false};
            const auto region_info = create_packed_regions_info(rois, rois, params);
            if (data.params.debug.draw_borders) {
                data.display_regions_queue.push({.sequence_id = sequence_id, .regions = region_info});
                data.metadata_runtime.record_bundle_enqueued();
            }
            if (data.params.debug.save_frames) {
                data.debug_regions_queue.push({.sequence_id = sequence_id, .regions = region_info});
                data.metadata_runtime.record_bundle_enqueued();
            }
            return;
        }

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
} // namespace

void qp_map_delta_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                const std::vector<sf::rectangle> &input_rectangles,
                                const std::vector<split_rectangle> &split_rectangles, const bool is_static,
                                const int frame_number) {
    const pipeline_app_adapter app_adapter(data);
    const auto frame_contract = get_frame_contract_from_buffer(input_buffer.get());
    if (!frame_contract.has_value()) {
        data.metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("qp_map_delta frame missing contract metadata");
        sf::error(roi_detection_processing, "[Frame %d] Missing frame contract metadata in qp_map_delta", frame_number);
        return;
    }

    pri_packer_view packer(data.packer);
    const auto config = make_packing_config(data);
    const auto packing_result =
            perform_detection_packing(packer, config, input_rectangles, split_rectangles, is_static, frame_number);
    const auto &object_rois = packing_result.original_rois;

    ensure_output_buffer_from_input(input_buffer, output_buffer, props, &data.memory_runtime);
    if (!output_buffer) {
        sf::error(roi_detection_processing, "[Frame %d] Failed to create output buffer for qp_map_delta mode",
                  frame_number);
        return;
    }

    const int ctb_size = std::max(1, data.params.qp_map_ctb_size);
    const auto qp_delta_map =
            build_qp_delta_map(props.width, props.height, ctb_size, data.params.qp_map_background_delta, object_rois);
    const auto grid = compute_ctb_grid(props.width, props.height, ctb_size);

    const auto object_ctb_count =
            static_cast<int>(std::count(qp_delta_map.begin(), qp_delta_map.end(), static_cast<int8_t>(0)));
    sf::debug(roi_detection_processing,
              "[Frame %d] Generated qp_map_delta map: ctb_size=%d grid=%dx%d object_ctbs=%d total_ctbs=%zu",
              frame_number, ctb_size, grid.columns, grid.rows, object_ctb_count, qp_delta_map.size());

    data.pending_qp_map_payload = qp_map_frame_payload{.frame_number = frame_number,
                                                       .width = props.width,
                                                       .height = props.height,
                                                       .ctb_size = ctb_size,
                                                       .background_delta = data.params.qp_map_background_delta,
                                                       .delta_map = qp_delta_map};

    publish_debug_regions(data, object_rois, props, frame_contract->sequence_id);
    data.packed_regions_queue.push(
            {.sequence_id = frame_contract->sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
    data.metadata_runtime.record_bundle_enqueued();
    copy_buffer_timing_fields(input_buffer, output_buffer);
}

void qp_map_delta_process_no_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                  sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props) {
    const pipeline_app_adapter app_adapter(data);
    const auto frame_contract = get_frame_contract_from_buffer(input_buffer.get());
    if (!frame_contract.has_value()) {
        data.metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("qp_map_delta no-roi frame missing contract metadata");
        sf::error(roi_detection_processing, "Missing frame contract metadata in qp_map_delta no-roi path");
        return;
    }

    ensure_output_buffer_from_input(input_buffer, output_buffer, props, &data.memory_runtime);
    if (!output_buffer) {
        sf::error(roi_detection_processing, "Failed to create output buffer for qp_map_delta no-ROI frame");
        return;
    }

    const int ctb_size = std::max(1, data.params.qp_map_ctb_size);
    const auto qp_delta_map =
            build_qp_delta_map(props.width, props.height, ctb_size, data.params.qp_map_background_delta, {});
    sf::debug(roi_detection_processing, "Generated qp_map_delta map for no-ROI frame with %zu CTBs",
              qp_delta_map.size());

    data.pending_qp_map_payload = qp_map_frame_payload{.frame_number = data.current_frame_number.load() - 1,
                                                       .width = props.width,
                                                       .height = props.height,
                                                       .ctb_size = ctb_size,
                                                       .background_delta = data.params.qp_map_background_delta,
                                                       .delta_map = qp_delta_map};

    if (data.params.debug.draw_borders) {
        data.display_regions_queue.push(
                {.sequence_id = frame_contract->sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
        data.metadata_runtime.record_bundle_enqueued();
    }
    if (data.params.debug.save_frames) {
        data.debug_regions_queue.push(
                {.sequence_id = frame_contract->sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
        data.metadata_runtime.record_bundle_enqueued();
    }
    data.packed_regions_queue.push(
            {.sequence_id = frame_contract->sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
    data.metadata_runtime.record_bundle_enqueued();
    copy_buffer_timing_fields(input_buffer, output_buffer);
}
