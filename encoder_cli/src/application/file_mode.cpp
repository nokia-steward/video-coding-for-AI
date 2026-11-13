// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "file_mode.h"
#include "../../libvcai/include/bitrate_calculation.h"
#include "../../libvcai/include/gst_raii.h"
#include "../encoder_cli_debug.h"
#include "../encoder_enums.h"
#include "../global_data.h"
#include "../pipeline/startup.h"
#include "../processing/pipeline_message_processing.h"
#include "../reporting/simple_status_reporter.h"
#include "app_periodic_gst_tasks.h"
#include "lifecycle_facade.h"
#include "thread_management.h"
#include "webrtc_connection.h"

#include <chrono>
#include <gst/gst.h>

/**
 * @file file_mode.cpp
 * @brief Implementation of file/camera mode application runner.
 */

namespace {
    /**
     * @brief Reports application startup event.
     * @param data The global data structure.
     */
    void report_application_startup(global_data &data) {
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->report_event("Application started - beginning video processing");
            data.monitoring.status_reporter->start_periodic_reporting(5);
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->start_periodic_reporting(data.params.performance_monitoring_interval_seconds);
        }
        (void) start_app_periodic_reporting_gst_task(data);
    }

    /**
     * @brief Reports application shutdown event.
     * @param data The global data structure.
     */
    void report_application_shutdown(global_data &data) {
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->stop_periodic_reporting();
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->stop_periodic_reporting();
            data.monitoring.perf_monitor->generate_final_summary();
        }
        join_app_periodic_reporting_gst_task(data);
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->report_event("Application shutdown initiated");
        }
    }
} // namespace

/**
 * @brief Runs the application in file/camera mode (non-UDP).
 *
 * This function handles the main application loop for file and camera input modes.
 * It waits for WebRTC connection if needed, starts processing threads, processes
 * pipeline messages, and handles graceful shutdown.
 *
 * @param data The global data structure.
 */
void run_file_mode(global_data &data) {
    // Wait for WebRTC connection if using WebRTC transport
    if (data.params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc) {
        if (!wait_for_webrtc_connection(data, std::chrono::seconds(30))) {
            return;
        }

        lifecycle_facade pipeline_runtime(data);
        if (!start_input_and_encoding_pipelines(pipeline_runtime)) {
            sf::error(encoder_cli_main, "Failed to start input/encoding pipelines after WebRTC connection");
            return;
        }
    }

    report_application_startup(data);

    const worker_thread_start_options worker_options{
            .start_input_message_thread = true,
            .start_output_message_thread = false,
    };
    auto worker_bundle = start_worker_threads(data, worker_options);
    process_pipeline_messages(data.output_pipeline, data);
    wait_for_worker_threads_completion(data, *worker_bundle);

    sf::calculate_and_print_bitrate(data.num_input_frames, data.params.framerate, data.h265_filesink);

    report_application_shutdown(data);
}
