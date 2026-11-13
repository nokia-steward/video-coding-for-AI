// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "camera_input_mode.h"
#include "../../../../libvcai/include/bitrate_calculation.h"
#include "../../../../libvcai/include/gst_raii.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../encoder_cli_debug.h"
#include "../../../encoder_enums.h"
#include "../../../global_data.h"
#include "../../../gst/input_types/camera_input.h"
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
#include <thread>

/**
 * @file camera_input_mode.cpp
 * @brief Implementation of camera input mode functions.
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
     * @brief Runs camera input mode execution loop.
     * @param data The narrow application-orchestration view.
     */
    void run_camera_input_mode(lifecycle_facade &data) {
        if (!wait_for_webrtc_connection_if_needed(data)) {
            return;
        }

        report_application_startup(data);

        const worker_thread_start_options worker_options{
                .start_input_message_thread = true,
                .start_output_message_thread = false,
        };
        auto worker_bundle = start_worker_threads(data.instance(), worker_options);
        process_pipeline_messages(data.output_pipeline, data.instance());
        wait_for_worker_threads_completion(data.instance(), *worker_bundle);

        sf::calculate_and_print_bitrate(data.num_input_frames, data.params.framerate, data.h265_filesink);

        report_application_shutdown(data);
    }
} // namespace

/**
 * @brief Creates function table for camera input mode.
 *
 * Creates a function table with lambdas for camera input mode operations.
 *
 * @return Function table for camera input mode.
 */
input_mode_functions create_camera_input_mode_functions() {
    return input_mode_functions{
            .is_enabled =
                    [](const global_data &data) {
                        // Camera mode is enabled when input_type is camera.
                        // Inference and non-inference pipelines are selected in get_pipeline_creator.
                        return data.params.input_type_value == input_type::camera;
                    },
            .get_pipeline_creator = [](const global_data &data) -> std::optional<std::function<bool(global_data &)>> {
                // If ROI detection is enabled, it handles all input types
                if (data.params.inference_engine) {
                    return create_roi_detection_input_pipeline;
                }
                // Otherwise, use camera input pipeline
                return create_camera_input_pipeline;
            },
            .run_mode = run_camera_input_mode,
            .get_mode_name = []() { return "camera"; }};
}

/**
 * @brief Creates input mode context for camera input mode.
 *
 * Creates and returns an input mode context containing the camera input mode
 * function table.
 *
 * @return Input mode context.
 */
input_mode_context create_camera_input_mode_context() {
    return input_mode_context{.functions = create_camera_input_mode_functions()};
}
