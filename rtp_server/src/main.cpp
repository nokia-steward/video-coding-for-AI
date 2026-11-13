// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <atomic>
#include <csignal>
#include <glib-unix.h>
#include <glib.h>
#include <gst/gst.h>

#include "area_copy.h"
#include "client/client_manager.h"
#include "client/client_session.h"
#include "client/session_monitor.h"
#include "core/command_line.h"
#include "core/configuration_manager.h"
#include "core/inference_engine_startup.h"
#include "core/rtp_config.h"
#include "core/server.h"
#include "gst/gpu_elements.h"
#include "logging.h"
#include "nal_utils.h"
#include "pipeline/output/display/display_branch.h"
#include "pipeline/pipeline.h"
#include "utils/rtp_statistics.h"

// Debug category for RTP server main entry point.
GST_DEBUG_CATEGORY_STATIC(rtp_server_main);
#define GST_CAT_DEFAULT rtp_server_main

/**
 * @file main.cpp
 * @brief The main entry point for the RTP server application.
 */

namespace {
    /**
     * @brief Global server pointer used by signal handlers to initiate shutdown.
     */
    std::atomic<rtp_server *> g_server_instance{nullptr};

    /**
     * @brief Setup signal handlers for graceful shutdown using GLib.
     * @param main_loop The main loop to quit when signals are received.
     */
    void setup_signal_handlers(GMainLoop *main_loop) {
#ifdef G_OS_UNIX
        // Use GLib's signal handling which integrates properly with g_main_loop_run
        // Using lambda for better integration
        auto signal_handler = []([[maybe_unused]] gpointer user_data) -> gboolean {
            sf::info(rtp_server_main, "Caught signal, initiating graceful shutdown...");

            if (const auto *server = g_server_instance.load()) {
                server->stop();
            } else {
                sf::warning(rtp_server_main, "No server instance found!");
            }

            return G_SOURCE_REMOVE;
        };

        const guint sigint_id = g_unix_signal_add(SIGINT, signal_handler, main_loop);
        if (sigint_id == 0) {
            sf::error(rtp_server_main, "Failed to register SIGINT signal handler");
        }
        const guint sigterm_id = g_unix_signal_add(SIGTERM, signal_handler, main_loop);
        if (sigterm_id == 0) {
            sf::error(rtp_server_main, "Failed to register SIGTERM signal handler");
        }

        if (sigint_id != 0 && sigterm_id != 0) {
            sf::info(rtp_server_main, "GLib signal handlers registered for graceful shutdown");
        }
#else
        sf::warning(rtp_server_main, "Signal handling not supported on this platform");
#endif
    }

    /**
     * @brief Initialize GStreamer and all project debug categories.
     */
    void initialize_gstreamer() {
        // Initialize GStreamer
        gst_init(nullptr, nullptr);

        // Initialize debug categories
        GST_DEBUG_CATEGORY_INIT(rtp_server_main, "rtp_server", 0, "RTP Server Main");
        init_pipeline_debug_category();
        init_server_debug_category();
        init_rtp_statistics_debug_category();
        init_client_session_debug_category();
        init_session_monitor_debug_category();
        init_command_line_debug_category();
        init_client_manager_debug_category();
        init_configuration_manager_debug_category();
        init_inference_engine_startup_debug_category();
        init_display_branch_debug_category();
        init_gpu_elements_debug_category();

        // Initialize libvcai debug categories (must be after gst_init())
        init_nal_utils_debug_category();
        init_area_copy_debug_category();

        sf::info(rtp_server_main, "GStreamer initialized successfully");
    }

    /**
     * @brief Main application logic for the RTP server.
     * @param handshake_port The port to listen for client connections.
     * @param monitoring_config RTP monitoring configuration.
     * @param rtp_output_config RTP output configuration.
     * @param output_directory Optional output directory for saving video files.
     * @return Exit code (0 for success, non-zero for error).
     */
    [[nodiscard]] int run_rtp_server(const guint16 handshake_port, const rtp_monitoring_config &monitoring_config,
                                     const rtp_output_config &rtp_output_config,
                                     const std::optional<std::string> &output_directory,
                                     const sf::background_color background_color,
                                     const transport_type transport_type_param, const bool draw_bitrate,
                                     const bool draw_fps, const rtp_server_config *full_config) {
        sf::info(rtp_server_main, "Starting RTP server on handshake port %u (transport: %s)...", handshake_port,
                 transport_type_to_string(transport_type_param));

        // Create and initialize the RTP server
        // Pass full config for WebRTC support
        rtp_server server(handshake_port, monitoring_config, rtp_output_config, output_directory, background_color,
                          transport_type_param, draw_bitrate, draw_fps, full_config);
        // Set global server instance for signal handling
        g_server_instance.store(&server);

        if (!server.initialize()) {
            sf::error(rtp_server_main, "Failed to initialize RTP server");
            g_server_instance.store(nullptr);
            return -1;
        }

        // Setup signal handlers with the main loop
        setup_signal_handlers(server.get_main_loop());

        sf::info(rtp_server_main, "RTP server initialized successfully, starting main loop...");

        // Run the server main loop
        const int result = server.run();

        sf::info(rtp_server_main, "RTP server main loop ended, cleaning up...");

        // Explicitly call cleanup to ensure logging is visible
        server.cleanup();

        // Clear global server instance
        g_server_instance.store(nullptr);

        sf::info(rtp_server_main, "RTP server shutdown completed successfully");
        return result;
    }
} // namespace

/**
 * @brief Main entry point for the RTP server application.
 * @param argc Number of command line arguments.
 * @param argv Array of command line argument strings.
 * @return Exit code (0 for success, non-zero for error).
 */
int main(const int argc, char *argv[]) {
    try {
        initialize_gstreamer();

        // Parse command line arguments
        const command_line_parameters cli_params = parse_command_line(argc, argv);

        // Load configuration from file and apply command line overrides
        const rtp_server_config config = load_and_configure_rtp_server(cli_params);

        sf::info(rtp_server_main, "RTP server configuration loaded - Handshake port: %u", config.handshake_port);
        sf::info(rtp_server_main, "Monitoring - Packet loss: %s, Jitter: %s, Silence: %s",
                 config.monitoring_config.enable_packet_loss_check ? "enabled" : "disabled",
                 config.monitoring_config.enable_jitter_check ? "enabled" : "disabled",
                 config.monitoring_config.enable_silence_check ? "enabled" : "disabled");
        sf::info(rtp_server_main, "RTP Output - %s, Host: %s, Port: %u, Payload Type: %u",
                 config.rtp_output.enabled ? "enabled" : "disabled", config.rtp_output.host.c_str(),
                 config.rtp_output.port, config.rtp_output.payload_type);
        if (config.output_directory.has_value()) {
            sf::info(rtp_server_main, "Output directory: %s (file saving enabled)", config.output_directory->c_str());
        } else {
            sf::info(rtp_server_main, "Output directory: not set (file saving disabled, display only)");
        }

        if (!check_gpu_elements_available(&config)) {
            sf::error(rtp_server_main,
                      "Required pipeline elements are unavailable. For nvh265dec ensure NVIDIA plugins/CUDA; for "
                      "openhevcdec ensure gst-openhevc is on GST_PLUGIN_PATH.");
            return 1;
        }

        if (!ensure_inference_engine_ready(config)) {
            sf::error(rtp_server_main, "Inference engine startup failed. Server will not start.");
            return 1;
        }

        // Run the RTP server
        const int result =
                run_rtp_server(config.handshake_port, config.monitoring_config, config.rtp_output,
                               config.output_directory, config.background_color, config.communication_transport_type,
                               config.draw_bitrate, config.draw_fps, &config);

        sf::info(rtp_server_main, "RTP server application exiting with code %d", result);
        return result;
    } catch (const std::exception &e) {
        sf::error(rtp_server_main, "Exception in main: %s", e.what());
        return 1;
    } catch (...) {
        sf::error(rtp_server_main, "Unknown exception in main");
        return 1;
    }
}
