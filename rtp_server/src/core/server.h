// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gio/gio.h>
#include <glib.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include "../transport/transport_interface.h"
#include "area_copy.h"
#include "communication/signaling.h"
#include "gst_raii.h"
#include "rtp_config.h"
#include "rtp_server_config.h"

// Forward declarations
struct rtp_pipeline;
struct client_manager;

/**
 * @brief RTP server struct for handling client connections and streaming.
 *
 * This struct manages the RTP server functionality including RTP input handshake,
 * client management, and pipeline creation for video streaming.
 */
struct rtp_server {
    /**
     * @brief Constructor.
     * @param handshake_port The port for client handshake (default: 5000).
     * @param monitoring_config RTP monitoring configuration.
     * @param rtp_output_config RTP output configuration.
     * @param output_directory Optional output directory for saving video files.
     * @param background_color Background color for empty buffers (default: 0).
     */
    explicit rtp_server(guint16 handshake_port = 5000,
                        const rtp_monitoring_config &monitoring_config = rtp_monitoring_config{},
                        const rtp_output_config &rtp_output_config_param = rtp_output_config{},
                        const std::optional<std::string> &output_directory = std::nullopt,
                        sf::background_color background_color = sf::background_color::green,
                        transport_type transport_type_param = transport_type::rtp, bool draw_bitrate = false,
                        bool draw_fps = false, const rtp_server_config *full_config = nullptr);

    /**
     * @brief Destructor.
     */
    ~rtp_server();

    // Disable copy constructor and assignment operator
    rtp_server(const rtp_server &) = delete;
    rtp_server &operator=(const rtp_server &) = delete;

    /**
     * @brief Initialize the server.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool initialize();

    /**
     * @brief Run the server main loop.
     * @return Exit code (0 for success).
     */
    [[nodiscard]] int run() const;

    /**
     * @brief Stop the server.
     */
    void stop() const;

    /**
     * @brief Get the handshake port.
     * @return The handshake port number.
     */
    [[nodiscard]] guint16 get_handshake_port() const { return handshake_port; }

    /**
     * @brief Set the handshake port.
     * @param port The new handshake port.
     */
    void set_handshake_port(const guint16 port) { handshake_port = port; }

    /**
     * @brief Get the main loop for signal handling.
     * @return The main loop pointer.
     */
    [[nodiscard]] GMainLoop *get_main_loop() const { return main_loop.get(); }

    /**
     * @brief Create the handshake socket (for RTP input transport).
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_handshake_socket();

    /**
     * @brief Setup socket watcher for incoming connections (for RTP input transport).
     */
    void setup_socket_watcher();

    /**
     * @brief Set the signaling server (for WebRTC transport).
     * @param signaling_server The signaling server to set (moved).
     */
    void set_signaling_server(
            std::unique_ptr<sf::communication::signaling_server, void (*)(sf::communication::signaling_server *)>
                    server);

    /**
     * @brief Get the signaling server (for WebRTC transport).
     * @return Pointer to signaling server, or nullptr if not set.
     */
    [[nodiscard]] sf::communication::signaling_server *get_signaling_server() const;

    /**
     * @brief Cleanup all resources.
     */
    void cleanup();

    /**
     * @brief Handles WebRTC client connection callback (public for transport functions).
     * @param client_id The client ID that connected.
     */
    void handle_webrtc_client_connected(const std::string &client_id);

    /**
     * @brief Handles WebRTC client disconnection callback (public for transport functions).
     * @param client_id The client ID that disconnected.
     */
    void handle_webrtc_client_disconnected(const std::string &client_id);

    /**
     * @brief Handles WebRTC signaling message callback (public for transport functions).
     * @param client_id The client ID.
     * @param msg The signaling message.
     */
    void handle_webrtc_signaling_message_callback(const std::string &client_id,
                                                  const sf::communication::signaling_message &msg);

private:
    /**
     * @brief Handle client connection request.
     * @param socket The socket that received the message.
     * @param condition The IO condition.
     * @param user_data User data (pointer to this server instance).
     * @return TRUE to continue watching, FALSE to stop.
     */
    static gboolean on_client_message(GSocket *socket, GIOCondition condition, gpointer user_data);

    /**
     * @brief Create a pipeline for a new client via client manager.
     * @param port The port number for the client.
     * @return true if pipeline was created and started successfully.
     */
    [[nodiscard]] bool create_client_pipeline(guint16 port) const;

    /**
     * @brief Allocate a new port for a client.
     * @return The allocated port number.
     */
    [[nodiscard]] guint16 allocate_client_port();

    /**
     * @brief Cleanup pipeline/session for a client port.
     * @param port The client streaming port.
     */
    void cleanup_client_by_port(guint16 port);

    /**
     * @brief Write test mode summaries for all active pipelines.
     * Called during shutdown to ensure summaries are written even without
     * explicit client disconnect.
     */
    void flush_test_mode_summaries();

    // Server configuration
    guint16 handshake_port;
    guint16 next_client_port;

    // GStreamer and GLib components
    sf::unique_g_main_loop_ptr main_loop;
    sf::unique_gsocket_ptr socket;
    sf::unique_g_socket_address_ptr listen_address;
    sf::unique_g_source_ptr socket_source;

    // Client management
    std::unique_ptr<client_manager> client_manager_ptr;

    // Configuration
    rtp_monitoring_config config;
    rtp_output_config rtp_output;
    std::optional<std::string> output_directory;
    sf::background_color background_color;
    bool draw_bitrate;
    bool draw_fps;

    // Shutdown state
    mutable std::atomic<bool> shutdown_initiated{false};
    bool test_mode_summaries_flushed{false};

    // Transport abstraction
    transport_context transport_ctx;

    // WebRTC signaling server (only used when transport_type=webrtc)
    std::unique_ptr<sf::communication::signaling_server, void (*)(sf::communication::signaling_server *)>
            signaling_server_ptr;
    transport_type communication_transport_type_enum; // Transport type enum
    std::optional<rtp_server_config> webrtc_config_data; // Full config for WebRTC parameters
    const rtp_server_config *full_config_ptr; // Non-owning pointer to full config (for all transport types)

    // WebRTC client ID to port mapping (for routing SDP/ICE messages)
    mutable std::map<std::string, guint16> webrtc_client_to_port;
    mutable std::map<guint16, std::string> webrtc_port_to_client;
};

void init_server_debug_category();
