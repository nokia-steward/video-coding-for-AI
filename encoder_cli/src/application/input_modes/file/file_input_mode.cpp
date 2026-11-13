// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "file_input_mode.h"
#include "../../../../libvcai/include/bitrate_calculation.h"
#include "../../../../libvcai/include/gst_raii.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../encoder_cli_debug.h"
#include "../../../encoder_enums.h"
#include "../../../global_data.h"
#include "../../../gst/input_types/file_input.h"
#include "../../../gst/input_types/roi_input.h"
#include "../../../pipeline/startup.h"
#include "../../../processing/pipeline_message_processing.h"
#include "../../../reporting/simple_status_reporter.h"
#include "../../app_periodic_gst_tasks.h"
#include "../../lifecycle_facade.h"
#include "../../thread_management.h"
#include "../../webrtc_connection.h"

#include <chrono>
#include <optional>

/**
 * @file file_input_mode.cpp
 * @brief Implementation of file input mode functions.
 */

namespace {
    /**
     * @brief Reports application startup event.
     * @param data The narrow application-orchestration view.
     */
    void report_application_startup(lifecycle_facade &data) {
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->report_event("Application started - beginning video processing");
            data.monitoring.status_reporter->start_periodic_reporting(5);
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->start_periodic_reporting(data.params.performance_monitoring_interval_seconds);
        }
        (void) start_app_periodic_reporting_gst_task(data.instance());
    }

    /**
     * @brief Reports application shutdown event.
     * @param data The narrow application-orchestration view.
     */
    void report_application_shutdown(lifecycle_facade &data) {
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->stop_periodic_reporting();
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->stop_periodic_reporting();
            data.monitoring.perf_monitor->generate_final_summary();
        }
        join_app_periodic_reporting_gst_task(data.instance());
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->report_event("Application shutdown initiated");
        }
    }

    /**
     * @brief Waits for WebRTC connection if using WebRTC transport.
     * @param data The narrow application-orchestration view.
     * @return true if connection is ready or not needed, false on timeout.
     */
    [[nodiscard]] bool wait_for_webrtc_connection_if_needed(lifecycle_facade &data) {
        if (data.params.communication.resolved_transport_type() != sf::communication::transport_type::webrtc) {
            return true;
        }

        if (!wait_for_webrtc_connection(data.instance(), std::chrono::seconds(30))) {
            return false;
        }

        // Start input and encoding pipelines now that WebRTC connection is ready
        if (!start_input_and_encoding_pipelines(data)) {
            sf::error(encoder_cli_main, "Failed to start input/encoding pipelines after WebRTC connection");
            return false;
        }

        return true;
    }

    /**
     * @brief Runs file input mode execution loop.
     * @param data The narrow application-orchestration view.
     */
    void run_file_input_mode(lifecycle_facade &data) {
        if (!wait_for_webrtc_connection_if_needed(data)) {
            return;
        }

        report_application_startup(data);

        sf::info(encoder_cli_main, "Starting file-mode processing worker threads");
        const worker_thread_start_options worker_options{
                .start_input_message_thread = true,
                .start_output_message_thread = true,
        };
        auto worker_bundle = start_worker_threads(data.instance(), worker_options);

        // File mode (non-WebRTC): put the input pipeline to PLAYING only after the consuming worker
        // threads are armed, so the finite clip is consumed by armed ROI/encoder-submission workers
        // instead of being EOS-closed early. start_all_pipelines deferred the input start in file mode;
        // for WebRTC the input/encoding pipelines were already started on connection in
        // wait_for_webrtc_connection_if_needed.
        if (data.params.communication.resolved_transport_type() != sf::communication::transport_type::webrtc) {
            if (!start_input_pipeline_only(data)) {
                sf::error(encoder_cli_main, "Failed to start input pipeline after arming file-mode workers");
            }
        }

        start_file_mode_watchdog_gst_task(data.instance());

        sf::info(encoder_cli_main, "Waiting for file-mode worker threads to complete");
        wait_for_file_mode_worker_threads_completion(data.instance(), *worker_bundle);
        join_file_mode_watchdog_gst_task(data.instance());
        sf::info(encoder_cli_main, "File-mode worker threads completed");
        if (!data.shutdown_started.load()) {
            sf::info(encoder_cli_main, "Triggering main-thread controlled global_data::shutdown");
            data.shutdown();
        } else {
            sf::info(encoder_cli_main, "global_data::shutdown already started before file-mode completion");
        }

        sf::calculate_and_print_bitrate(data.num_input_frames, data.params.framerate, data.h265_filesink);

        report_application_shutdown(data);
    }
} // namespace

/**
 * @brief Creates function table for file input mode.
 *
 * Creates a function table with lambdas for file input mode operations.
 *
 * @return Function table for file input mode.
 */
input_mode_functions create_file_input_mode_functions() {
    return input_mode_functions{
            .is_enabled =
                    [](const global_data &data) {
                        // File mode is enabled when input_type is file.
                        // Inference and non-inference pipelines are selected in get_pipeline_creator.
                        return data.params.input_type_value == input_type::file;
                    },
            .get_pipeline_creator = [](const global_data &data) -> std::optional<std::function<bool(global_data &)>> {
                // If ROI detection is enabled, it handles all input types
                if (data.params.inference_engine) {
                    return create_roi_detection_input_pipeline;
                }
                // Otherwise, use file input pipeline
                return create_file_input_pipeline;
            },
            .run_mode = run_file_input_mode,
            .get_mode_name = []() { return "file"; }};
}

/**
 * @brief Creates input mode context for file input mode.
 *
 * Creates and returns an input mode context containing the file input mode
 * function table.
 *
 * @return Input mode context.
 */
input_mode_context create_file_input_mode_context() {
    return input_mode_context{.functions = create_file_input_mode_functions()};
}
