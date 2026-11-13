// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../../libvcai/include/logging.h"
#include "../../bridge/app/pipeline_app_adapter.h"
#include "../../encoder_backend/encoder_backend.h"
#include "../../global_data.h"
#include "../../gst/input_types/frame_path_meta.h"
#include "../../gst/input_types/rtp_timestamp_overlay.h"
#include "../../reporting/performance_monitor.h"
#include "../../roi/pri_packer_view.h"
#include "../frame_processing_utils.h"
#include "../roi_detection_packing.h"
#include "../roi_detection_processing.h"
#include "detection_modes/detection_factory.h"
#include "helpers.h"
#include "packing_modes/packing_factory.h"
#include "processing_modes/processing_factory.h"
#include "roi_strategy_snapshot.h"

#include <atomic>
#include <gst/gst.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/**
 * @file api.cpp
 * @brief Implementation of ROI detection processing API functions.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    /**
     * @brief Builds the narrow availability/config snapshot for ROI mode selection.
     * @param data Global data.
     * @return Snapshot consumed by the mode factories.
     */
    [[nodiscard]] roi_strategy_snapshot make_roi_strategy_snapshot(const global_data &data) {
        roi_strategy_snapshot snapshot;
        snapshot.inference_engine = data.params.inference_engine;
        snapshot.groundtruth_roi_available =
                data.loaded_groundtruth_roi_data.has_value() && !data.loaded_groundtruth_roi_data->empty();
        snapshot.static_roi_available =
                data.loaded_static_roi_data.has_value() && !data.loaded_static_roi_data->empty();
        snapshot.pack_rois = data.params.pack_rois;
        snapshot.packer_type_value = data.params.packer_type_value;
        snapshot.effective_background_mode_value = data.params.effective_background_mode_value;
        return snapshot;
    }

    /**
     * @brief Gets frame properties and increments frame number.
     * @param data Global data.
     * @param sample Sample to get properties from.
     * @return Frame properties or std::nullopt on error.
     */
    [[nodiscard]] std::optional<frame_properties>
    get_frame_properties_and_increment(global_data &data, const sf::unique_gst_sample_ptr &sample) {
        const auto props_opt = get_frame_properties(sample);
        if (!props_opt) {
            sf::error(roi_detection_processing, "Could not get frame properties, skipping frame.");
            return std::nullopt;
        }

        // Increment frame number for tracking
        data.current_frame_number.fetch_add(1);
        return props_opt;
    }

    /**
     * @brief Pushes output buffer to encoding pipeline.
     * @param data Global data.
     * @param output_buffer Output buffer to push.
     */
    void push_buffer_to_encoder(global_data &data, sf::unique_gst_buffer_ptr output_buffer,
                                std::optional<qp_map_frame_payload> qp_map_payload) {
        if (!output_buffer) {
            sf::error(roi_detection_processing, "Output buffer is null, cannot push to encoder");
            return;
        }

        if (!data.active_encoder_backend) {
            sf::error(roi_detection_processing, "No encoder backend configured");
            return;
        }

        if (!data.active_encoder_backend->submit_frame(data, std::move(output_buffer), std::move(qp_map_payload))) {
            sf::error(roi_detection_processing, "Encoder backend failed to submit frame");
        }
    }

} // namespace

void process_sample(global_data &data, sf::unique_gst_sample_ptr &sample);

namespace roi_det_gst_detail {

    enum class roi_detection_iter_result { Continue, ExitLoop };

    [[nodiscard]] roi_detection_iter_result roi_detection_frame_processing_iteration(global_data &data,
                                                                                     pipeline_app_adapter &app_adapter,
                                                                                     guint64 &processed_frames) {
        auto sample_opt = data.raw_frame_queue.pop();
        if (!sample_opt) {
            sf::info(roi_detection_processing, "ROI detection processing thread finished.");
            return roi_detection_iter_result::ExitLoop;
        }

        sf::debug(roi_detection_processing, "Popped frame from raw_frame_queue, processing");
        ::process_sample(data, *sample_opt);
        processed_frames++;

        if (app_adapter.should_force_close_file_raw_queue(processed_frames)) {
            const bool first_forced_close = app_adapter.mark_file_input_forced_raw_eos_once();
            if (first_forced_close) {
                sf::warning(roi_detection_processing,
                            "File input ROI fallback reached expected frame count (%llu). Closing raw frame queue and "
                            "terminating ROI loop.",
                            static_cast<unsigned long long>(processed_frames));
            }
            app_adapter.close_raw_queue_once();
            return roi_detection_iter_result::ExitLoop;
        }

        return roi_detection_iter_result::Continue;
    }

    void run_roi_detection_epilogue([[maybe_unused]] global_data &data, pipeline_app_adapter &app_adapter) {
        sf::info(roi_detection_processing, "ROI processing shutdown: closing packed regions queue");
        app_adapter.close_metadata_queues_once();
        if (app_adapter.has_active_encoder_backend()) {
            if (app_adapter.is_watchdog_forced_shutdown()) {
                sf::warning(
                        roi_detection_processing,
                        "ROI processing shutdown: watchdog-triggered forced shutdown active; skipping encoder backend "
                        "EOS flush to avoid known qp_map_delta stall path");
                app_adapter.close_encoded_queue_once();
                return;
            }
            sf::info(roi_detection_processing, "ROI processing shutdown: signaling EOS to encoder backend");
            app_adapter.signal_encoder_backend_eos();
            sf::info(roi_detection_processing, "ROI processing shutdown: encoder backend EOS completed");
        }
    }

    struct roi_detection_task_userdata {
        global_data *data = nullptr;
        std::unique_ptr<pipeline_app_adapter> app_adapter{};
        guint64 processed_frames = 0;
        GstTask *self_task = nullptr;
        std::atomic<bool> epilogue_done{false};
    };

    void roi_detection_epilogue_once(roi_detection_task_userdata *ud) {
        bool expected = false;
        if (!ud->epilogue_done.compare_exchange_strong(expected, true)) {
            return;
        }
        run_roi_detection_epilogue(*ud->data, *ud->app_adapter);
    }

    void roi_detection_task_entry(gpointer user_data) {
        auto *ud = static_cast<roi_detection_task_userdata *>(user_data);
        const auto step = roi_detection_frame_processing_iteration(*ud->data, *ud->app_adapter, ud->processed_frames);
        if (step == roi_detection_iter_result::ExitLoop) {
            roi_detection_epilogue_once(ud);
            gst_task_stop(GST_TASK(ud->self_task));
        }
    }

    void destroy_roi_detection_task_userdata(gpointer user_data) {
        delete static_cast<roi_detection_task_userdata *>(user_data);
    }

} // namespace roi_det_gst_detail

/**
 * @brief Processes a single sample through ROI detection and packing.
 * @param data Global data context.
 * @param sample Input sample to process.
 */
void process_sample(global_data &data, sf::unique_gst_sample_ptr &sample) {
    const auto start_time = std::chrono::steady_clock::now();
    const pipeline_app_adapter app_adapter(data);

    const auto props_opt = get_frame_properties_and_increment(data, sample);
    if (!props_opt) {
        return;
    }

    const int frame_number = data.current_frame_number.load() - 1; // Get the frame number we just incremented

    sf::debug(roi_detection_processing, "Processing sample for frame %d", frame_number);
    data.pending_qp_map_payload.reset();

    // Get buffer from sample - we'll make a writable copy later if needed
    auto *input_buffer_raw = gst_sample_get_buffer(sample.get());
    const auto frame_contract = get_frame_contract_from_buffer(input_buffer_raw);
    if (!frame_contract.has_value()) {
        data.metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("raw frame missing contract metadata");
        sf::error(roi_detection_processing, "Skipping frame due to missing frame contract metadata");
        return;
    }
    if (!data.metadata_runtime.validate_and_advance(data.metadata_runtime.expected_raw_sequence,
                                                    frame_contract->sequence_id)) {
        app_adapter.request_metadata_contract_fail_stop("raw frame sequence mismatch");
        sf::error(roi_detection_processing, "Raw frame contract sequence mismatch (expected=%llu, observed=%llu)",
                  static_cast<unsigned long long>(data.metadata_runtime.expected_raw_sequence.load()),
                  static_cast<unsigned long long>(frame_contract->sequence_id));
        return;
    }

    const sf::unique_gst_buffer_no_delete_ptr detection_buffer(input_buffer_raw);

    // Narrow availability/config snapshot for mode selection
    const auto strategy_snapshot = make_roi_strategy_snapshot(data);

    // Get active detection mode
    const auto *detection_mode = get_active_detection_mode(strategy_snapshot);
    if (!detection_mode) {
        sf::error(roi_detection_processing, "No detection mode enabled");
        return;
    }

    // Detect ROIs using active detection mode
    auto [input_rectangles, split_rectangles] =
            detection_mode->functions.detect_rois(data, detection_buffer, *props_opt, frame_number);

    const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_raw);

    // Get tracked objects for status reporting
    const auto tracked_objects = detection_mode->functions.get_tracked_objects(data);

    // Create output buffer with correct size based on frame properties.
    // Background blur mode owns output allocation internally.
    sf::unique_gst_buffer_ptr output_buffer;
    if (!data.params.is_background_blur_effective()) {
        output_buffer = create_output_buffer(input_buffer, *props_opt);
        if (!output_buffer) {
            sf::error(roi_detection_processing, "Could not create output buffer, skipping frame.");
            return;
        }
    }

    // Log detection debug info for inference and static modes
    const std::string mode_name = detection_mode->name ? detection_mode->name : "";
    if (mode_name == "inference" || mode_name == "static" || mode_name == "groundtruth") {
        log_detection_debug_info(input_rectangles, frame_number);
    }

    // Update status reporter with ROI detection statistics
    app_adapter.record_roi_detection_update(input_rectangles.size(), tracked_objects.size(), detection_mode->name);

    // Handle random_access packing mode (if enabled)
    if (data.params.pack_rois) {
        const auto *packing_mode = get_active_packing_mode(strategy_snapshot);
        if (packing_mode && std::string(packing_mode->name) == "random_access") {
            pri_packer_view packer(data.packer);
            const auto config = make_packing_config(data);
            if (packing_mode->functions.process_frame(packer, config, sample, input_rectangles, split_rectangles,
                                                      mode_name == "static" || mode_name == "groundtruth",
                                                      frame_number)) {
                // Frame was handled by random_access packer
                return;
            }
        }
    }

    // Get active processing mode
    const auto *processing_mode = get_active_processing_mode(strategy_snapshot);
    if (!processing_mode) {
        sf::error(roi_detection_processing, "No processing mode enabled");
        return;
    }

    // Process frame using active processing mode
    if (input_rectangles.empty()) {
        processing_mode->functions.process_no_rois(data, input_buffer, output_buffer, *props_opt);
    } else {
        const bool is_static = (mode_name == "static" || mode_name == "groundtruth");
        processing_mode->functions.process_frame(data, input_buffer, output_buffer, *props_opt, input_rectangles,
                                                 split_rectangles, is_static, frame_number);
    }

    if (output_buffer && data.params.debug.draw_rtp_timestamp) {
        if (!draw_rtp_timestamp_on_buffer(output_buffer.get(), *props_opt)) {
            sf::debug(roi_detection_processing, "RTP timestamp not drawn on output buffer for frame %d", frame_number);
        }
    }

    if (output_buffer && !attach_frame_contract_metadata_to_buffer(output_buffer.get(), frame_contract->path,
                                                                   frame_contract->sequence_id)) {
        data.metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("failed to attach contract metadata to encoder-bound frame");
        sf::error(roi_detection_processing, "Skipping encoder push due to output contract metadata attach failure");
        return;
    }

    // Push buffer to encoder
    push_buffer_to_encoder(data, std::move(output_buffer), std::move(data.pending_qp_map_payload));

    // Record timing for ROI detection processing
    app_adapter.record_stage_timing(monitoring_stage::roi_detection_processing, start_time,
                                    std::chrono::steady_clock::now());
}

/**
 * @brief The main processing loop for the ROI detection thread.
 *
 * This function runs in a separate thread, continuously popping raw video frames
 * from a queue, extracting ROI information, processing them through the ROI manager,
 * and pushing the results to the next stage of the pipeline.
 *
 * @param data A reference to the shared global_data struct.
 */
void roi_detection_frame_processing(global_data &data) {
    pipeline_app_adapter app_adapter(data);
    sf::info(roi_detection_processing, "ROI detection processing thread started");
    guint64 processed_frames = 0;
    using roi_det_gst_detail::roi_detection_frame_processing_iteration;
    using roi_det_gst_detail::roi_detection_iter_result;
    using roi_det_gst_detail::run_roi_detection_epilogue;
    while (roi_detection_frame_processing_iteration(data, app_adapter, processed_frames) ==
           roi_detection_iter_result::Continue) {
    }
    run_roi_detection_epilogue(data, app_adapter);
}

bool start_roi_detection_gst_task(global_data &data, bypass_gst_poll_slot &slot) {
    if (slot.task != nullptr) {
        return false;
    }

    using roi_det_gst_detail::destroy_roi_detection_task_userdata;
    using roi_det_gst_detail::roi_detection_task_entry;
    using roi_det_gst_detail::roi_detection_task_userdata;

    // RAII_EXEMPT: GstTask user_data; freed in destroy_roi_detection_task_userdata.
    auto *ud = new roi_detection_task_userdata{};
    ud->data = &data;
    ud->app_adapter = std::make_unique<pipeline_app_adapter>(data);

    GstTask *const task = gst_task_new(roi_detection_task_entry, ud, destroy_roi_detection_task_userdata);
    if (!task) {
        delete ud;
        sf::error(roi_detection_processing, "Failed to create GstTask for ROI detection processing");
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &slot.lock);
    sf::info(roi_detection_processing, "ROI detection GstTask starting");
    if (!gst_task_start(task)) {
        sf::error(roi_detection_processing, "Failed to start GstTask for ROI detection processing");
        gst_object_unref(task);
        return false;
    }

    slot.task = task;
    return true;
}

void join_roi_detection_gst_task(global_data & /*data*/, bypass_gst_poll_slot &slot) {
    if (slot.task == nullptr) {
        return;
    }
    GstTask *const task = slot.task;
    slot.task = nullptr;

    gst_task_stop(task);
    if (!gst_task_join(task)) {
        sf::warning(roi_detection_processing, "gst_task_join failed for ROI detection GstTask");
    }
    gst_object_unref(task);
}
