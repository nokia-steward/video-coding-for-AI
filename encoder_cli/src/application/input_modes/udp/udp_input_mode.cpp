// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "udp_input_mode.h"
#include "../../../../libvcai/include/gst_raii.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../encoder_cli_debug.h"
#include "../../../encoder_enums.h"
#include "../../../global_data.h"
#include "../../../gst/input_types/input_ingress_facade.h"
#include "../../../gst/input_types/roi_input.h"
#include "../../../gst/input_types/udp_input.h"
#include "../../../pipeline/startup.h"
#include "../../../processing/pipeline_message_processing.h"
#include "../../../reporting/simple_status_reporter.h"
#include "../../app_periodic_gst_tasks.h"
#include "../../lifecycle_facade.h"
#include "../../thread_management.h"
#include "../../webrtc_connection.h"

#include <chrono>
#include <gst/gst.h>
#include <optional>
#include <thread>

/**
 * @file udp_input_mode.cpp
 * @brief Implementation of UDP input mode functions.
 */

namespace {
    constexpr int timeout_seconds = 10;
    constexpr int check_interval_ms = 100;

    /**
     * @brief Logs UDP stream settings.
     * @param data The narrow application-orchestration view.
     */
    void log_udp_stream_settings(const lifecycle_facade &data) {
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
     * @param data The narrow application-orchestration view.
     * @return true if client connected, false if shutdown requested.
     */
    [[nodiscard]] bool wait_for_udp_client_connection(lifecycle_facade &data) {
        sf::info(encoder_cli_main, "Waiting for UDP client connection...");
        log_udp_stream_settings(data);

        const input_ingress_facade ingress(data.instance());
        const bool already_connected = has_udp_client_connected(ingress);
        if (already_connected) {
            sf::info(encoder_cli_main, "UDP client already connected, proceeding immediately");
        }

        while (!has_udp_client_connected(ingress) && !data.stop_processing.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
        }

        const bool connected = has_udp_client_connected(ingress);
        sf::info(encoder_cli_main, "UDP client connection check complete: connected=%d", connected ? 1 : 0);
        return !data.stop_processing.load();
    }

    /**
     * @brief Reports UDP client connection event.
     * @param data The narrow application-orchestration view.
     */
    void report_udp_client_connected(lifecycle_facade &data) {
        sf::info(encoder_cli_main, "UDP client connected: Starting video processing");
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->report_event("UDP client connected - beginning video processing");
            data.monitoring.status_reporter->start_periodic_reporting(5);
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->start_periodic_reporting(data.params.performance_monitoring_interval_seconds);
        }
        (void) start_app_periodic_reporting_gst_task(data.instance());
    }

    /**
     * @brief Checks for UDP connection timeout.
     * @return true if timeout occurred, false otherwise.
     */
    [[nodiscard]] bool check_udp_timeout(lifecycle_facade &data) {
        const input_ingress_facade ingress(data.instance());
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
     * @param data The narrow application-orchestration view.
     * @return true if error occurred, false otherwise.
     */
    [[nodiscard]] bool check_pipeline_messages_for_errors(lifecycle_facade &data) {
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
     * @param data The narrow application-orchestration view.
     * @return true if timeout occurred, false if shutdown requested.
     */
    [[nodiscard]] bool monitor_udp_connection(lifecycle_facade &data) {
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
     * @param data The narrow application-orchestration view.
     * @param timeout_occurred Whether timeout occurred.
     */
    void report_udp_connection_end(lifecycle_facade &data, const bool timeout_occurred) {
        if (data.monitoring.status_reporter) {
            data.monitoring.status_reporter->stop_periodic_reporting();
        }
        if (data.monitoring.perf_monitor) {
            data.monitoring.perf_monitor->stop_periodic_reporting();
            data.monitoring.perf_monitor->generate_final_summary();
        }
        join_app_periodic_reporting_gst_task(data.instance());
        if (data.monitoring.status_reporter) {
            const char *event_message = timeout_occurred ? "UDP connection timeout - resetting for new client"
                                                         : "UDP connection ended - resetting for new client";
            data.monitoring.status_reporter->report_event(event_message);
        }
    }

    /**
     * @brief Resets state for next UDP connection.
     * @param data The narrow application-orchestration view.
     */
    void reset_for_next_udp_connection(lifecycle_facade &data) {
        data.reset_processing_state();
        input_ingress_facade ingress(data.instance());
        reset_udp_connection_state(ingress);
        sf::info(encoder_cli_main, "Reset complete: Ready for new UDP client connection");
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
     * @brief Runs UDP input mode execution loop.
     * @param data The narrow application-orchestration view.
     */
    void run_udp_input_mode(lifecycle_facade &data) {
        sf::info(encoder_cli_main, "=== run_udp_input_mode() called ===");
        const bool is_webrtc_mode =
                data.params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc;
        sf::info(encoder_cli_main, "WebRTC mode: %s", is_webrtc_mode ? "true" : "false");

        // For WebRTC mode, wait for connection before checking for UDP client
        // For UDP mode, wait for UDP client first
        if (is_webrtc_mode) {
            if (!wait_for_webrtc_connection_if_needed(data)) {
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
            auto worker_bundle = start_worker_threads(data.instance(), worker_options);

            const bool timeout_occurred = monitor_udp_connection(data);

            report_udp_connection_end(data, timeout_occurred);

            (void) data.request_worker_stop_once(true);
            wait_for_worker_threads_completion(data.instance(), *worker_bundle);

            reset_for_next_udp_connection(data);

            if (data.stop_processing.load()) {
                break;
            }
        }
    }
} // namespace

/**
 * @brief Creates function table for UDP input mode.
 *
 * Creates a function table with lambdas for UDP input mode operations.
 *
 * @return Function table for UDP input mode.
 */
input_mode_functions create_udp_input_mode_functions() {
    return input_mode_functions{.is_enabled =
                                        [](const global_data &data) {
                                            // UDP mode is enabled when input_type is udp
                                            // Note: UDP always uses ROI detection pipeline (even when inference_engine
                                            // is disabled)
                                            return data.params.input_type_value == input_type::udp;
                                        },
                                .get_pipeline_creator = []([[maybe_unused]] const global_data &data)
                                        -> std::optional<std::function<bool(global_data &)>> {
                                    // UDP input always requires ROI detection pipeline infrastructure
                                    return create_roi_detection_input_pipeline;
                                },
                                .run_mode = run_udp_input_mode,
                                .get_mode_name = []() { return "udp"; }};
}

/**
 * @brief Creates input mode context for UDP input mode.
 *
 * Creates and returns an input mode context containing the UDP input mode
 * function table.
 *
 * @return Input mode context.
 */
input_mode_context create_udp_input_mode_context() {
    return input_mode_context{.functions = create_udp_input_mode_functions()};
}
