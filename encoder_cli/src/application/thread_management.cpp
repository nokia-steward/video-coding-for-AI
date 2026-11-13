// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "thread_management.h"
#include "../../libvcai/include/logging.h"
#include "../encoder_cli_debug.h"
#include "../global_data.h"
#include "../gst/input_types/appsink_polling.h"
#include "../gst/input_types/input_ingress_facade.h"
#include "../processing/encoded_frame_processing.h"
#include "../processing/encoder_submission_processing.h"
#include "../processing/pipeline_message_processing.h"
#include "../processing/roi_detection_processing.h"
#include "../processing/roi_detection_random_access.h"
#include "../roi/random_access_packer.h"

#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <variant>

/**
 * @file thread_management.cpp
 * @brief Implementation of thread management functions.
 */

namespace {
    /**
     * @brief Closes random access packer if needed.
     * @param data The global data structure.
     */
    void close_random_access_packer(const global_data &data) {
        if (!data.params.pack_rois) {
            return;
        }

        if (const auto *ra_packer = std::get_if<std::unique_ptr<random_access_packer>>(&data.packer)) {
            if (*ra_packer) {
                (*ra_packer)->close();
            }
        }
    }

    void wait_for_file_input_ingress_completion(global_data &data) {
        if (!data.expected_file_input_frames.has_value()) {
            return;
        }

        constexpr auto ingress_timeout = std::chrono::seconds(120);
        const auto deadline = std::chrono::steady_clock::now() + ingress_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (data.num_input_frames >= *data.expected_file_input_frames &&
                data.ingress_runtime.raw_queue_closed.load()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        sf::warning(encoder_cli_main,
                    "File mode: timed out waiting for input ingress completion (frames=%llu expected=%llu, "
                    "raw_queue_closed=%s)",
                    static_cast<unsigned long long>(data.num_input_frames),
                    static_cast<unsigned long long>(*data.expected_file_input_frames),
                    data.ingress_runtime.raw_queue_closed.load() ? "true" : "false");
    }

} // namespace

void wait_for_threads_completion(bypass_gst_poll_slot &roi_detection_slot, bypass_gst_poll_slot &random_access_slot,
                                 bypass_gst_poll_slot &encoded_frame_slot, global_data &data) {
    sf::info(encoder_cli_main, "Waiting for processing threads to complete");
    join_roi_detection_gst_task(data, roi_detection_slot);
    close_random_access_packer(data);

    join_random_access_gst_task(data, random_access_slot);

    join_encoded_frames_gst_task(data, encoded_frame_slot);
    sf::info(encoder_cli_main, "Processing threads completed");
}

std::unique_ptr<worker_thread_bundle> start_worker_threads(global_data &data,
                                                           const worker_thread_start_options &options) {
    data.begin_worker_lifecycle_cycle();
    auto bundle = std::make_unique<worker_thread_bundle>();

    sf::info(encoder_cli_main, "Starting ROI detection GstTask");
    if (!start_roi_detection_gst_task(data, bundle->roi_detection_slot)) {
        sf::error(encoder_cli_main, "Failed to start ROI detection GstTask");
    }

    if (!start_random_access_gst_task(data, bundle->random_access_slot)) {
        sf::error(encoder_cli_main, "Failed to start random-access GstTask (when configured)");
    }

    data.ready_to_process_frames = true;
    sf::info(encoder_cli_main, "System ready to process frames - frame dropping disabled");
    sf::info(encoder_cli_main, "ROI frame processing workers started");

    if (!start_encoded_frames_gst_task(data, bundle->encoded_frame_slot)) {
        sf::error(encoder_cli_main, "Failed to start encoded frame processing GstTask");
    }

    if (data.params.input_type_value == input_type::file && data.active_encoder_backend &&
        data.active_encoder_backend->requires_gst_encoding_pipeline()) {
        if (!start_encoder_submission_gst_task(data, bundle->encoder_submission_slot)) {
            sf::error(encoder_cli_main, "Failed to start encoder submission GstTask for file mode");
        }
    }

    data.mark_processing_workers_active(true);

    bool any_message_task = false;
    if (options.start_input_message_thread && data.input_pipeline) {
        sf::info(encoder_cli_main, "Starting input pipeline message GstTask");
        if (start_pipeline_message_gst_task(data, data.input_pipeline.get(), bundle->input_message_slot)) {
            any_message_task = true;
        } else {
            sf::error(encoder_cli_main, "Failed to start input pipeline message GstTask");
        }
    }
    if (options.start_output_message_thread && data.output_pipeline) {
        sf::info(encoder_cli_main, "Starting output pipeline message GstTask");
        if (start_pipeline_message_gst_task(data, data.output_pipeline.get(), bundle->output_message_slot)) {
            any_message_task = true;
        } else {
            sf::error(encoder_cli_main, "Failed to start output pipeline message GstTask");
            join_pipeline_message_gst_task(bundle->input_message_slot);
        }
    }

    data.mark_message_workers_active(any_message_task);

    return bundle;
}

void wait_for_worker_threads_completion(global_data &data, worker_thread_bundle &bundle) {
    wait_for_threads_completion(bundle.roi_detection_slot, bundle.random_access_slot, bundle.encoded_frame_slot, data);
    data.mark_processing_workers_active(false);

    (void) data.request_worker_stop_once(true);
    join_pipeline_message_gst_task(bundle.input_message_slot);
    join_pipeline_message_gst_task(bundle.output_message_slot);
    data.mark_message_workers_active(false);
    data.mark_worker_lifecycle_drained();
}

void wait_for_file_mode_worker_threads_completion(global_data &data, worker_thread_bundle &bundle) {
    wait_for_file_input_ingress_completion(data);

    sf::info(encoder_cli_main, "Waiting for file-mode ROI processing GstTask to complete");
    join_roi_detection_gst_task(data, bundle.roi_detection_slot);
    close_random_access_packer(data);

    data.close_encoder_submission_queue_once();
    join_encoder_submission_gst_task(data, bundle.encoder_submission_slot);

    if (data.active_encoder_backend && data.active_encoder_backend->requires_gst_encoding_pipeline()) {
        sf::info(encoder_cli_main, "File mode: signaling encoder EOS after submission queue drain");
        data.active_encoder_backend->signal_eos(data);

        constexpr auto flush_timeout = std::chrono::seconds(60);
        sf::info(encoder_cli_main, "File mode: waiting for encoded contract backlog to drain after encoder EOS");
        if (!data.wait_for_pending_encoded_contract_drain(flush_timeout, false)) {
            sf::warning(encoder_cli_main,
                        "File mode: timed out waiting for encoded contract drain before joining encoded GstTask");
            data.clear_pending_encoded_contract_states();
            data.close_encoded_queue_once();
        }
    }

    join_random_access_gst_task(data, bundle.random_access_slot);
    join_encoded_frames_gst_task(data, bundle.encoded_frame_slot);
    data.mark_processing_workers_active(false);

    (void) data.request_worker_stop_once(true);
    join_pipeline_message_gst_task(bundle.input_message_slot);
    join_pipeline_message_gst_task(bundle.output_message_slot);
    data.mark_message_workers_active(false);
    data.mark_worker_lifecycle_drained();
    sf::info(encoder_cli_main, "File-mode processing threads completed");
}

/**
 * @brief Starts bypass polling threads if bypass feature is enabled.
 *
 * Creates threads for polling inference appsink, bypass appsink, and consuming
 * merged frames from the frame merger.
 *
 * @param data The global data structure.
 * @return Tuple of polling threads (inference, bypass, merger) or empty tuple if bypass disabled.
 */
bool start_bypass_polling_threads(global_data &data) {
    input_ingress_facade ingress(data);
    if (!data.params.performance.enable_deepstream_bypass) {
        return true;
    }

    if (!data.bypass.merger || !data.bypass.appsink_inference || !data.bypass.appsink_bypass) {
        sf::error(encoder_cli_main, "Bypass infrastructure not initialized");
        return false;
    }

    sf::info(encoder_cli_main, "Starting bypass polling workers (GstTask appsinks + merger)");

    appsink_poll_context inference_ctx;
    inference_ctx.appsink = data.bypass.appsink_inference;
    inference_ctx.merger = data.bypass.merger.get();
    inference_ctx.data = &data;
    inference_ctx.stop_flag = &data.stop_processing;
    inference_ctx.eos_counter = &data.bypass.appsink_eos_count;
    inference_ctx.is_inference_path = true;

    appsink_poll_context bypass_ctx;
    bypass_ctx.appsink = data.bypass.appsink_bypass;
    bypass_ctx.merger = data.bypass.merger.get();
    bypass_ctx.data = &data;
    bypass_ctx.stop_flag = &data.stop_processing;
    bypass_ctx.eos_counter = &data.bypass.appsink_eos_count;
    bypass_ctx.is_inference_path = false;

    if (!start_appsink_poll_gst_task(data.bypass.inference_poll_slot, inference_ctx, "Inference")) {
        return false;
    }
    if (!start_appsink_poll_gst_task(data.bypass.bypass_poll_slot, bypass_ctx, "Bypass")) {
        join_appsink_poll_gst_task(data.bypass.inference_poll_slot);
        return false;
    }

    if (!start_merger_poll_gst_task(ingress, data.bypass.merger_poll_slot)) {
        join_appsink_poll_gst_task(data.bypass.bypass_poll_slot);
        join_appsink_poll_gst_task(data.bypass.inference_poll_slot);
        return false;
    }

    data.mark_polling_workers_active(true);
    return true;
}

void restart_bypass_polling_threads_if_needed(global_data &data) {
    if (!data.params.performance.enable_deepstream_bypass) {
        return;
    }
    if (!data.input_pipeline) {
        return;
    }
    if (data.bypass.inference_poll_slot.task != nullptr || data.bypass.bypass_poll_slot.task != nullptr ||
        data.bypass.merger_poll_slot.task != nullptr) {
        return;
    }
    if (data.bypass.appsink_inference == nullptr || data.bypass.appsink_bypass == nullptr) {
        sf::warning(encoder_cli_main, "Bypass appsinks unavailable; skipping polling-thread restart");
        return;
    }

    if (!data.bypass.merger || data.bypass.merger->is_closed()) {
        frame_merger_config merger_config;
        merger_config.max_buffer_frames = data.params.performance.merger_buffer_size;
        merger_config.max_latency_ms = data.params.performance.merger_max_latency_ms;
        merger_config.emergency_drop_threshold = merger_config.max_buffer_frames + 5;
        data.bypass.merger = std::make_unique<frame_merger>(merger_config);
        data.bypass.appsink_eos_count.store(0);
        sf::info(encoder_cli_main, "Recreated bypass frame merger for new worker lifecycle generation");
    }

    if (!start_bypass_polling_threads(data)) {
        sf::warning(encoder_cli_main, "Failed to restart bypass polling workers");
    }
}

void join_bypass_polling_threads(global_data &data) {
    input_ingress_facade ingress(data);
    join_appsink_poll_gst_task(data.bypass.inference_poll_slot);
    join_appsink_poll_gst_task(data.bypass.bypass_poll_slot);
    join_merger_poll_gst_task(ingress, data.bypass.merger_poll_slot);
    data.bypass.appsink_eos_count.store(0);
    data.mark_polling_workers_active(false);
}
