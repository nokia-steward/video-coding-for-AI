// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "../core/rtp_config.h"
#include "../core/rtp_server_config.h"
#include "../transport/transport_interface.h"
#include "area_copy.h"
#include "client_session.h"
#include "session_monitor.h"

// Forward declarations
struct rtp_pipeline;

/**
 * @brief Client connection and pipeline management.
 *
 * This class handles client connections, pipeline creation,
 * and overall client lifecycle management.
 */
struct client_manager {
    /**
     * @brief Constructor.
     * @param main_loop The main loop for pipeline operations.
     * @param monitoring_config RTP monitoring configuration.
     * @param rtp_output_config RTP output configuration.
     * @param output_directory Optional output directory for saving video files.
     * @param background_color Background color for empty buffers (default: green, which is 0 in YUV).
     * @param transport_ctx Transport context with function table and user data.
     * @param draw_bitrate Enable bitrate display overlay (default: false).
     * @param draw_bitrate_large Enable large red bitrate display overlay (2x size, red color) (default: false).
     * @param draw_fps Enable FPS display overlay (default: false).
     * @param server_config Optional pointer to full server configuration (for DeepStream inference).
     */
    explicit client_manager(GMainLoop *main_loop, const rtp_monitoring_config &monitoring_config,
                            const rtp_output_config &rtp_output_config_param,
                            const std::optional<std::string> &output_directory = std::nullopt,
                            sf::background_color background_color = sf::background_color::green,
                            transport_context transport_ctx_param = {.functions = {}}, bool draw_bitrate = false,
                            bool draw_bitrate_large = false, bool draw_fps = false,
                            const rtp_server_config *server_config = nullptr);

    /**
     * @brief Destructor.
     */
    ~client_manager();

    // Disable copy constructor and assignment operator
    client_manager(const client_manager &) = delete;
    client_manager &operator=(const client_manager &) = delete;

    /**
     * @brief Get the client sessions map.
     * @return Reference to the client sessions map.
     */
    std::map<guint16, std::unique_ptr<client_session>> &get_client_sessions() { return client_sessions; }

    /**
     * @brief Get the client pipelines map.
     * @return Reference to the client pipelines map.
     */
    std::map<guint16, std::unique_ptr<rtp_pipeline>> &get_client_pipelines() { return client_pipelines; }

    /**
     * @brief Get the session monitor.
     * @return Reference to the session monitor.
     */
    session_monitor &get_session_monitor() { return monitor; }

    /**
     * @brief Create a pipeline for a new client.
     * @param port The port number for the client.
     * @return true if pipeline is created and running, false otherwise.
     */
    [[nodiscard]] bool create_client_pipeline(guint16 port);

private:
    GMainLoop *main_loop;
    std::map<guint16, std::unique_ptr<rtp_pipeline>> client_pipelines;
    std::map<guint16, std::unique_ptr<client_session>> client_sessions;
    session_monitor monitor;
    rtp_monitoring_config config;
    rtp_output_config rtp_output;
    std::optional<std::string> output_directory;
    sf::background_color background_color;
    transport_context transport_ctx; // Transport abstraction
    bool draw_bitrate;
    bool draw_bitrate_large;
    bool draw_fps;
    const rtp_server_config *server_config; // Non-owning pointer to server config
};

/**
 * @brief Initialize the client manager debug category.
 */
void init_client_manager_debug_category();
