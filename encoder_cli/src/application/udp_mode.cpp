// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "udp_mode.h"
#include "../../libvcai/include/gst_raii.h"
#include "../../libvcai/include/logging.h"
#include "../encoder_cli_debug.h"
#include "../global_data.h"
#include "../gst/input_types/input_ingress_facade.h"
#include "../gst/input_types/udp_input.h"
#include "../pipeline/startup.h"
#include "../reporting/simple_status_reporter.h"
#include "app_periodic_gst_tasks.h"
#include "lifecycle_facade.h"
#include "thread_management.h"
#include "webrtc_connection.h"

#include <chrono>
#include <gst/gst.h>
#include <thread>

/**
 * @file udp_mode.cpp
 * @brief Implementation of UDP mode application runner with connection management.
 */

namespace {
    constexpr int timeout_seconds = 10;
    constexpr int check_interval_ms = 100;

    /**
     * @brief Logs UDP stream settings.
     * @param data The global data structure.
     */
    void log_udp_stream_settings(const global_data &data) {
        sf::info(encoder_cli_main,
                 "UDP stream settings: bind_address=%s, port=%u, encoding=%s, resolution=%dx%d, framerate=%d "
                 "fps, payload_type=%u, clock_rate=%u, latency=%d ms",
                 data.params.udp.resolved_bind_address().c_str(), data.params.udp.resolved_port(),
                 data.params.udp.resolved_encoding().c_str(), data.params.width, data.params.height,
                 data.params.framerate, data.params.udp.resolved_payload_type(), data.params.udp.resolved_clock_rate(),
                 data.params.udp.resolved_latency_ms());
    }

    /**
     * @brief Waits for UDP client connection.
     * @param data The global data structure.
     * @return true if client connected, false if shutdown requested.
     */
    [[nodiscard]] bool wait_for_udp_client_connection(global_data &data) {
        sf::info(encoder_cli_main, "Waiting for UDP client connection...");
        log_udp_stream_settings(data);

        const input_ingress_facade ingress(data);
        while (!has_udp_client_connected(ingress) && !data.stop_processing.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        }

        return !data.stop_processing.load();
    }

    /**
     * @brief Reports UDP client connection event.
     * @param data The global data structure.
     */
    void report_udp_client_connected(global_data &data) {
        sf::info(encoder_cli_main, "UDP client connected: Starting video processing");
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->report_event("UDP client connected - beginning video processing");
            data.monitoring.status_reporter->start_periodic_reporting(5);
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->start_periodic_reporting(data.params.performance_monitoring_interval_seconds);
        }
        (void) start_app_periodic_reporting_gst_task(data);
    }

    /**
     * @brief Checks for UDP connection timeout.
     * @return true if timeout occurred, false otherwise.
     */
    [[nodiscard]] bool check_udp_timeout(global_data &data) {
        const input_ingress_facade ingress(data);
        const auto last_packet_time = get_last_udp_packet_time(ingress);
        if (last_packet_time == std::chrono::steady_clock::time_point::min()) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_packet_time);

        if (elapsed.count() >= timeout_seconds) {
            sf::warning(encoder_cli_main, "UDP connection timeout: No data received for %d seconds", timeout_seconds);
            return true;
        }

        return false;
    }

    /**
     * @brief Checks pipeline messages for errors.
     * @param data The global data structure.
     * @return true if error occurred, false otherwise.
     */
    [[nodiscard]] bool check_pipeline_messages_for_errors(global_data &data) {
        const auto bus = sf::unique_gst_bus_ptr(gst_element_get_bus(data.output_pipeline.get()));
        const auto msg = sf::unique_gst_message_ptr(gst_bus_timed_pop_filtered(
                bus.get(), 250 * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING)));

        if (!msg) {
            return false;
        }

        if (GST_MESSAGE_TYPE(msg.get()) == GST_MESSAGE_ERROR) {
            GError *err_raw = nullptr;
            gchar *dbg_info_raw = nullptr;
            gst_message_parse_error(msg.get(), &err_raw, &dbg_info_raw);
            const auto err = sf::unique_g_error_ptr(err_raw);
            const auto dbg_info = sf::unique_g_char_ptr(dbg_info_raw);
            sf::error(encoder_cli_main, "ERROR from element %s: %s", GST_OBJECT_NAME(msg->src), err->message);
            sf::error(encoder_cli_main, "Debugging info: %s", dbg_info.get() ? dbg_info.get() : "none");
            if (data.monitoring.status_reporter) {
                data.monitoring.status_reporter->report_error();
            }
            data.shutdown();
            return true;
        }

        return false;
    }

    /**
     * @brief Monitors UDP connection and processes messages.
     * @param data The global data structure.
     * @return true if timeout occurred, false if shutdown requested.
     */
    [[nodiscard]] bool monitor_udp_connection(global_data &data) {
        while (!data.stop_processing.load()) {
            if (check_udp_timeout(data)) {
                return true;
            }

            if (check_pipeline_messages_for_errors(data)) {
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        }

        return false;
    }

    /**
     * @brief Reports UDP connection end event.
     * @param data The global data structure.
     * @param timeout_occurred Whether timeout occurred.
     */
    void report_udp_connection_end(global_data &data, const bool timeout_occurred) {
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->stop_periodic_reporting();
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->stop_periodic_reporting();
            data.monitoring.perf_monitor->generate_final_summary();
        }
        join_app_periodic_reporting_gst_task(data);
        if (data.monitoring.status_reporter) {
            if (timeout_occurred) {
                data.monitoring.status_reporter->report_event("UDP connection timeout - resetting for new client");
            } else {
                data.monitoring.status_reporter->report_event("UDP connection ended - resetting for new client");
            }
        }
    }

    /**
     * @brief Resets state for next UDP connection.
     * @param data The global data structure.
     */
    void reset_for_next_udp_connection(global_data &data) {
        data.reset_processing_state();
        input_ingress_facade ingress(data);
        reset_udp_connection_state(ingress);
        sf::info(encoder_cli_main, "Reset complete: Ready for new UDP client connection");
    }
} // namespace

/**
 * @brief Runs the application in UDP mode with connection timeout and reconnection loop.
 *
 * This function handles UDP client connections, monitors for timeouts, and manages
 * reconnection cycles. It supports both UDP and WebRTC transport modes.
 *
 * @param data The global data structure.
 */
void run_udp_mode(global_data &data) {
    const bool is_webrtc_mode =
            data.params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc;

    // For WebRTC mode, wait for connection before checking for UDP client
    // For UDP mode, wait for UDP client first
    if (is_webrtc_mode) {
        if (!wait_for_webrtc_connection(data, std::chrono::seconds(30))) {
            return;
        }

        // Start input and encoding pipelines now that WebRTC connection is ready
        lifecycle_facade pipeline_runtime(data);
        if (!start_input_and_encoding_pipelines(pipeline_runtime)) {
            sf::error(encoder_cli_main, "Failed to start input/encoding pipelines after WebRTC connection");
            return;
        }
    }

    while (!data.stop_processing.load()) {
        if (!is_webrtc_mode) {
            // Only wait for UDP client in non-WebRTC mode
            if (!wait_for_udp_client_connection(data)) {
                break;
            }
            report_udp_client_connected(data);
        } else {
            // In WebRTC mode, skip UDP client wait and proceed directly
            sf::info(encoder_cli_main, "WebRTC mode: Skipping UDP client wait, proceeding with frame processing");
        }

        const worker_thread_start_options worker_options{
                .start_input_message_thread = true,
                .start_output_message_thread = false,
        };
        auto worker_bundle = start_worker_threads(data, worker_options);

        const bool timeout_occurred = monitor_udp_connection(data);

        report_udp_connection_end(data, timeout_occurred);

        (void) data.request_worker_stop_once(true);
        wait_for_worker_threads_completion(data, *worker_bundle);

        reset_for_next_udp_connection(data);

        if (data.stop_processing.load()) {
            break;
        }
    }
}
