// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "client_manager.h"
#include <gst/gst.h>

#include "logging.h"

// Debug category for client manager operations.
GST_DEBUG_CATEGORY_STATIC(client_manager_category);
#define GST_CAT_DEFAULT client_manager_category

void init_client_manager_debug_category() {
    GST_DEBUG_CATEGORY_INIT(client_manager_category, "client-manager", 0, "Client Connection and Pipeline Management");
}

client_manager::client_manager(GMainLoop *loop, const rtp_monitoring_config &monitoring_config,
                               const rtp_output_config &rtp_output_config_param,
                               const std::optional<std::string> &output_directory_param,
                               const sf::background_color background_color_param,
                               const transport_context transport_ctx_param, const bool draw_bitrate_param,
                               const bool draw_bitrate_large_param, const bool draw_fps_param,
                               const rtp_server_config *server_config_param) :
    main_loop(loop),
    client_pipelines(), client_sessions(), monitor(), config(monitoring_config), rtp_output(rtp_output_config_param),
    output_directory(output_directory_param), background_color(background_color_param),
    transport_ctx(transport_ctx_param), draw_bitrate(draw_bitrate_param), draw_bitrate_large(draw_bitrate_large_param),
    draw_fps(draw_fps_param), server_config(server_config_param) {}

bool client_manager::create_client_pipeline(const guint16 port) {
    sf::info(client_manager_category, "Creating pipeline for port %u using rtp_pipeline class...", port);

    // Create the rtp_pipeline
    auto pipeline =
            std::make_unique<rtp_pipeline>(port, main_loop, rtp_output, output_directory, background_color,
                                           transport_ctx, draw_bitrate, draw_bitrate_large, draw_fps, server_config);
    if (!pipeline->create_pipeline()) {
        sf::error(client_manager_category, "Failed to create rtp_pipeline for port %u", port);
        return false;
    }

    // Note: For WebRTC, webrtcbin signals will be setup in rtp_server::create_client_pipeline
    // after the pipeline is stored in client_pipelines

    if (!pipeline->start_pipeline()) {
        sf::error(client_manager_category, "Failed to start rtp_pipeline for port %u", port);
        return false;
    }

    // Store the pipeline
    client_pipelines[port] = std::move(pipeline);

    // Create client session
    auto session = std::make_unique<client_session>(port, client_pipelines, config);

    // Connect pipeline with client session for RTP statistics
    if (const auto pipeline_it = client_pipelines.find(port); pipeline_it != client_pipelines.end()) {
        pipeline_it->second->set_client_session(session.get());
    }

    client_sessions[port] = std::move(session);

    // Start monitoring thread if this is the first client
    if (client_sessions.size() == 1 && !monitor.is_monitoring()) {
        monitor.start_monitoring(client_sessions, client_pipelines);
    }

    sf::info(client_manager_category, "RTP pipeline for port %u is running with frame processing.", port);
    return true;
}

client_manager::~client_manager() {
    sf::info(client_manager_category, "Client manager destructor called, cleaning up all clients...");

    // Stop session monitoring first
    monitor.stop_monitoring();

    // Stop all pipelines before destroying them
    for (auto &[port, pipeline]: client_pipelines) {
        sf::info(client_manager_category, "Stopping pipeline for port %u during client manager cleanup", port);
        pipeline->stop_pipeline();
    }

    // Clear all pipelines and sessions (they will be destroyed in reverse order)
    client_pipelines.clear();
    client_sessions.clear();

    sf::info(client_manager_category, "Client manager cleanup completed");
}
