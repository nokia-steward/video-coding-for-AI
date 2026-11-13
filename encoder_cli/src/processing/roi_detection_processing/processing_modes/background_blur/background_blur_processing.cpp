// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "background_blur_processing.h"
#include "../../../../../../libvcai/include/background_blur.h"
#include "../../../../../../libvcai/include/logging.h"
#include "../../../../bridge/app/pipeline_app_adapter.h"
#include "../../../../global_data.h"
#include "../../../../gst/input_types/frame_path_meta.h"
#include "../../../../roi/pri_packer_view.h"
#include "../../../../utils/packed_regions_info_utils.h"
#include "../../../frame_processing_utils.h"
#include "../../../roi_detection_packing.h"
#include "../../../roi_detection_utils.h"

#include <gst/gst.h>
#include <string>
#include <vector>

/**
 * @file background_blur_processing.cpp
 * @brief Implementation of background blur processing mode.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

/**
 * @brief Processes a frame with ROIs using background blur mode.
 * @param data Global data
 * @param input_buffer Input buffer
 * @param output_buffer Output buffer (may be modified)
 * @param props Frame properties
 * @param input_rectangles Detected rectangles
 * @param split_rectangles Split rectangles
 * @param is_static Whether this is static ROI mode
 * @param frame_number Frame number
 */
void background_blur_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                   sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                   const std::vector<sf::rectangle> &input_rectangles,
                                   const std::vector<split_rectangle> &split_rectangles, const bool is_static,
                                   const int frame_number) {
    const pipeline_app_adapter app_adapter(data);
    const auto frame_contract = get_frame_contract_from_buffer(input_buffer.get());
    if (!frame_contract.has_value()) {
        data.metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("background blur frame missing contract metadata");
        sf::error(roi_detection_processing, "[Frame %d] Missing frame contract metadata", frame_number);
        return;
    }

    // Perform packing to get accumulated ROIs (for blur exclusion)
    pri_packer_view packer(data.packer);
    const auto config = make_packing_config(data);
    const auto packing_result =
            perform_detection_packing(packer, config, input_rectangles, split_rectangles, is_static, frame_number);
    const auto &original_rois = packing_result.original_rois;

    // Create our own output buffer and copy input to it
    // This avoids modifying buffers that are part of samples
    output_buffer = create_output_buffer(input_buffer, props);
    if (!output_buffer) {
        sf::error(roi_detection_processing, "[Frame %d] Failed to create output buffer for background blur",
                  frame_number);
        return;
    }

    // Copy input frame data to output buffer
    if (!copy_video_frame_data(input_buffer, output_buffer, props, &data.memory_runtime)) {
        sf::error(roi_detection_processing, "[Frame %d] Failed to copy video frame data for background blur",
                  frame_number);
        return;
    }

    // Apply background blur to our output buffer (which we own)
    sf::unique_gst_buffer_no_delete_ptr output_wrapper(output_buffer.get());
    const sf::video_format blur_format_enum =
            props.format ? sf::string_to_video_format(props.format) : sf::video_format::i420;
    if (!sf::apply_background_blur(output_wrapper, original_rois, data.params.sigma, props.width, props.height,
                                   blur_format_enum, data.params.kernel_size)) {
        sf::error(roi_detection_processing, "[Frame %d] Failed to apply background blur", frame_number);
        return;
    }

    if ((data.params.debug.draw_borders || data.params.debug.save_frames) && !original_rois.empty()) {
        const creation_params params = {.reconstructed_width = props.width,
                                        .reconstructed_height = props.height,
                                        .log2_unit_size = 0,
                                        .persistence_flag = false};
        const auto region_info = create_packed_regions_info(original_rois, original_rois, params);
        if (data.params.debug.draw_borders) {
            data.display_regions_queue.push({.sequence_id = frame_contract->sequence_id, .regions = region_info});
            data.metadata_runtime.record_bundle_enqueued();
        }
        if (data.params.debug.save_frames) {
            data.debug_regions_queue.push({.sequence_id = frame_contract->sequence_id, .regions = region_info});
            data.metadata_runtime.record_bundle_enqueued();
        }
    } else {
        if (data.params.debug.draw_borders) {
            data.display_regions_queue.push({.sequence_id = frame_contract->sequence_id,
                                             .regions = sf::packed_regions_info::create_sentinel()});
            data.metadata_runtime.record_bundle_enqueued();
        }
        if (data.params.debug.save_frames) {
            data.debug_regions_queue.push({.sequence_id = frame_contract->sequence_id,
                                           .regions = sf::packed_regions_info::create_sentinel()});
            data.metadata_runtime.record_bundle_enqueued();
        }
    }

    // PRI SEI messages are disabled when background_blur is true
    // Push sentinel to keep queue synchronized (encoded_frame_processing will skip SEI injection)
    data.packed_regions_queue.push(
            {.sequence_id = frame_contract->sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
    data.metadata_runtime.record_bundle_enqueued();

    // Copy timestamps and flags from input buffer
    const GstClockTime pts = GST_BUFFER_PTS(input_buffer.get());
    const GstClockTime dts = GST_BUFFER_DTS(input_buffer.get());
    const GstClockTime duration = GST_BUFFER_DURATION(input_buffer.get());
    const guint64 offset = GST_BUFFER_OFFSET(input_buffer.get());
    const guint64 offset_end = GST_BUFFER_OFFSET_END(input_buffer.get());
    const guint flags = GST_BUFFER_FLAGS(input_buffer.get());

    GST_BUFFER_PTS(output_buffer.get()) = pts;
    GST_BUFFER_DTS(output_buffer.get()) = dts;
    GST_BUFFER_DURATION(output_buffer.get()) = duration;
    GST_BUFFER_OFFSET(output_buffer.get()) = offset;
    GST_BUFFER_OFFSET_END(output_buffer.get()) = offset_end;
    GST_BUFFER_FLAGS(output_buffer.get()) = flags;

    sf::debug(roi_detection_processing, "[Frame %d] Applied background blur with sigma=%.2f, kernel_size=%d",
              frame_number, data.params.sigma, data.params.kernel_size);
}

/**
 * @brief Processes a frame with no ROIs using background blur mode.
 * @param data Global data
 * @param input_buffer Input buffer
 * @param output_buffer Output buffer (may be modified)
 * @param props Frame properties
 */
void background_blur_process_no_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                     sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props) {
    const pipeline_app_adapter app_adapter(data);
    const auto frame_contract = get_frame_contract_from_buffer(input_buffer.get());
    if (!frame_contract.has_value()) {
        data.metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("background blur no-roi frame missing contract metadata");
        sf::error(roi_detection_processing, "Missing frame contract metadata in background blur no-roi path");
        return;
    }

    // Background blur with no ROIs - create output buffer, copy, and blur
    output_buffer = create_output_buffer(input_buffer, props);
    if (!output_buffer) {
        sf::error(roi_detection_processing, "Failed to create output buffer for background blur");
        return;
    }

    // Copy input frame data to output buffer
    if (!copy_video_frame_data(input_buffer, output_buffer, props, &data.memory_runtime)) {
        sf::error(roi_detection_processing, "Failed to copy video frame data for background blur");
        return;
    }

    // Apply background blur to our output buffer (blur entire frame)
    sf::unique_gst_buffer_no_delete_ptr output_wrapper(output_buffer.get());
    const sf::video_format blur_format_enum =
            props.format ? sf::string_to_video_format(props.format) : sf::video_format::i420;
    if (!sf::apply_background_blur(output_wrapper, {}, data.params.sigma, props.width, props.height, blur_format_enum,
                                   data.params.kernel_size)) {
        sf::error(roi_detection_processing, "Failed to apply background blur to frame");
        return;
    }
    // PRI SEI messages are disabled when background_blur is true
    data.packed_regions_queue.push(
            {.sequence_id = frame_contract->sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
    data.metadata_runtime.record_bundle_enqueued();
}
