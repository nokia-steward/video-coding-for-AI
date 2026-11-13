// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rtp_utils.h"
#include "../../libvcai/include/communication/manager.h"
#include "../../libvcai/include/logging.h"

#include <gio/gio.h>
#include <gst/gst.h>

/**
 * @file rtp_utils.cpp
 * @brief Implementation of RTP-related utility functions.
 */

/// GStreamer debug category for RTP utilities.
GST_DEBUG_CATEGORY_STATIC(rtp_utils);

void init_rtp_utils_debug_category() { GST_DEBUG_CATEGORY_INIT(rtp_utils, "rtp-utils", 0, "RTP utility functions"); }

/**
 * @brief Tests basic connectivity to the RTP server.
 * @param server_host The RTP server host address.
 * @param server_port The RTP server port for handshake.
 * @return true if server is reachable, false otherwise.
 */
bool test_rtp_server_connectivity(const std::string &server_host, const uint16_t server_port) {
    sf::info(rtp_utils, "Testing connectivity to RTP server at %s:%u...", server_host.c_str(), server_port);
    sf::info(rtp_utils, "Attempting to connect to RTP server (timeout: 2 seconds)...");

    GSocket *socket = nullptr;
    GError *error = nullptr;

    // Create server address
    GInetAddress *inet_address = g_inet_address_new_from_string(server_host.c_str());
    if (!inet_address) {
        sf::error(rtp_utils, "Invalid IP address: %s", server_host.c_str());
        return false;
    }

    GSocketAddress *server_address = g_inet_socket_address_new(inet_address, server_port);
    g_object_unref(inet_address);

    if (!server_address) {
        sf::error(rtp_utils, "Failed to create server address for %s:%u", server_host.c_str(), server_port);
        return false;
    }

    // Create UDP socket
    socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error);
    if (!socket) {
        sf::error(rtp_utils, "Error creating socket: %s", error->message);
        g_clear_error(&error);
        g_object_unref(server_address);
        return false;
    }

    // Set socket timeout for send and receive operations (2 seconds)
    g_socket_set_timeout(socket, 2);

    // Send actual handshake message to verify server is listening and responding
    // This ensures we're testing actual connectivity, not just the ability to send packets
    const auto handshake_msg = "RTP_HANDSHAKE";

    if (const gssize sent =
                g_socket_send_to(socket, server_address, handshake_msg, strlen(handshake_msg), nullptr, &error);
        sent < 0) {
        if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
            sf::error(rtp_utils, "RTP server connection timeout - server at %s:%u is not reachable",
                      server_host.c_str(), server_port);
        } else {
            sf::error(rtp_utils, "Failed to reach RTP server at %s:%u: %s", server_host.c_str(), server_port,
                      error ? error->message : "Unknown error");
        }
        g_clear_error(&error);
        g_object_unref(socket);
        g_object_unref(server_address);
        return false;
    }

    // Wait for server response - server should respond with a port number
    char buffer[1024];
    const gssize received = g_socket_receive(socket, buffer, sizeof(buffer) - 1, nullptr, &error);
    g_object_unref(socket);
    g_object_unref(server_address);

    if (received < 0) {
        if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
            // Timeout means server is not responding - connectivity test fails
            sf::error(rtp_utils, "RTP server at %s:%u is not responding (timeout waiting for handshake response)",
                      server_host.c_str(), server_port);
            sf::error(rtp_utils, "Please ensure the RTP server is running and accessible");
        } else {
            sf::error(rtp_utils, "Unexpected error receiving response: %s", error ? error->message : "Unknown error");
        }
        g_clear_error(&error);
        return false;
    }

    // If we received data, server is responding - verify it's a valid port number response
    buffer[received] = '\0';
    const std::string response(buffer);

    // Server should respond with a port number (digits only)
    try {
        const int parsed_port = std::stoi(response);
        // Validate port is in valid range before casting to uint16_t
        if (parsed_port > 0 && parsed_port <= 65535) {
            const auto assigned_port = static_cast<uint16_t>(parsed_port);
            sf::info(rtp_utils, "RTP server at %s:%u is reachable and responding (assigned test port: %u)",
                     server_host.c_str(), server_port, assigned_port);
            // Note: This test handshake allocates a port, but it will be cleaned up when the connection
            // is not used, or we can reuse this port for the actual handshake
            return true;
        }
    } catch ([[maybe_unused]] const std::exception &) {
        // Response is not a valid port number, but server did respond
        sf::warning(rtp_utils, "RTP server at %s:%u responded with unexpected message: '%s'", server_host.c_str(),
                    server_port, response.c_str());
        return true; // Server is responding, even if format is unexpected
    }

    sf::info(rtp_utils, "RTP server at %s:%u is reachable and responding", server_host.c_str(), server_port);
    return true;
}

/**
 * @brief Performs RTP handshake with the server to get an RTP port.
 * @param server_host The RTP server host address.
 * @param server_port The RTP server port for handshake.
 * @return The assigned RTP port and session id, or std::nullopt if handshake failed.
 */
std::optional<rtp_handshake_result> perform_rtp_handshake(const std::string &server_host, const uint16_t server_port,
                                                          const std::string &transport_type,
                                                          const std::string &webrtc_signaling_url) {
    sf::info(rtp_utils, "Contacting communication server at %s:%u for a streaming port...", server_host.c_str(),
             server_port);
    const auto transport_timeout =
            transport_type == "rtp" ? std::chrono::milliseconds(30000) : std::chrono::milliseconds(5000);
    sf::info(rtp_utils, "Performing communication handshake with server (transport: %s, timeout: %.1f seconds)...",
             transport_type.c_str(), static_cast<double>(transport_timeout.count()) / 1000.0);

    // Determine transport type
    sf::communication::transport_type comm_transport_type;
    if (transport_type == "webrtc") {
        comm_transport_type = sf::communication::transport_type::webrtc;
    } else if (transport_type == "rtp") {
        comm_transport_type = sf::communication::transport_type::udp;
    } else {
        sf::error(rtp_utils, "Unsupported transport type: %s", transport_type.c_str());
        return std::nullopt;
    }

    // Create communication manager with selected transport
    sf::info(rtp_utils, "Creating communication manager with transport type: %d",
             static_cast<int>(comm_transport_type));
    const auto manager = sf::communication::create_communication_manager(comm_transport_type);
    if (!manager) {
        sf::error(rtp_utils, "Failed to create communication manager");
        return std::nullopt;
    }
    sf::info(rtp_utils, "Communication manager created successfully");

    // Set WebRTC config if needed
    if (comm_transport_type == sf::communication::transport_type::webrtc) {
        if (webrtc_signaling_url.empty()) {
            sf::error(rtp_utils, "WebRTC transport requires signaling_url");
            return std::nullopt;
        }
        sf::info(rtp_utils, "Setting WebRTC config: signaling_url=%s", webrtc_signaling_url.c_str());
        sf::communication::webrtc_config webrtc_cfg{};
        webrtc_cfg.signaling_url = webrtc_signaling_url;
        webrtc_cfg.ice_timeout_ms = 10000;
        sf::communication::manager_set_webrtc_config(manager.get(), webrtc_cfg);
        sf::info(rtp_utils, "WebRTC config set successfully");
    }

    // Configure transport
    const sf::communication::transport_config config{
            .host = server_host, .port = server_port, .timeout = transport_timeout};
    sf::info(rtp_utils, "Transport config: host=%s, port=%u, timeout=%ld ms", config.host.c_str(), config.port,
             config.timeout.count());

    // Initialize and connect
    sf::info(rtp_utils, "Initializing communication manager...");
    const sf::communication::transport_error init_error = sf::communication::manager_initialize(manager.get(), config);
    if (init_error != sf::communication::transport_error::success) {
        sf::error(rtp_utils, "Failed to initialize communication manager: %d", static_cast<int>(init_error));
        return std::nullopt;
    }
    sf::info(rtp_utils, "Communication manager initialized successfully");

    // Perform handshake
    sf::info(rtp_utils, "Calling manager_perform_handshake...");
    const auto handshake_result = sf::communication::manager_perform_handshake(manager.get());
    sf::info(rtp_utils, "manager_perform_handshake returned, error: %d, has_result: %d",
             static_cast<int>(handshake_result.error), handshake_result.result.has_value() ? 1 : 0);
    if (handshake_result.error != sf::communication::transport_error::success || !handshake_result.result) {
        sf::error(rtp_utils, "Handshake failed: %d", static_cast<int>(handshake_result.error));
        return std::nullopt;
    }

    const uint16_t rtp_port = handshake_result.result->assigned_port;
    const uint32_t session_id = handshake_result.result->session_id;
    sf::info(rtp_utils, "Server assigned RTP port: %u (session_id: %u)", rtp_port, session_id);

    if (transport_type == "rtp") {
        sf::info(rtp_utils, "Waiting for stream-ready notification from server for port %u...", rtp_port);
        const auto ready_error = sf::communication::manager_wait_for_stream_ready(manager.get(), rtp_port, session_id);
        if (ready_error != sf::communication::transport_error::success) {
            sf::error(rtp_utils, "Stream-ready wait failed: %d", static_cast<int>(ready_error));
            [[maybe_unused]] const sf::communication::transport_error shutdown_error =
                    sf::communication::manager_shutdown(manager.get());
            return std::nullopt;
        }
        sf::info(rtp_utils, "Stream-ready received for port %u (session_id: %u)", rtp_port, session_id);
    }

    // Shutdown manager (transport will be cleaned up automatically)
    [[maybe_unused]] const sf::communication::transport_error shutdown_error =
            sf::communication::manager_shutdown(manager.get());

    return rtp_handshake_result{rtp_port, session_id};
}

bool send_rtp_disconnect(const std::string &server_host, const uint16_t server_port, const std::string &transport_type,
                         const std::string &webrtc_signaling_url, const uint16_t streaming_port,
                         const uint32_t session_id) {
    if (streaming_port == 0) {
        sf::warning(rtp_utils, "Disconnect requested with streaming_port=0; skipping");
        return false;
    }

    sf::info(rtp_utils, "Sending disconnect request to %s:%u for port %u (session_id: %u)...", server_host.c_str(),
             server_port, streaming_port, session_id);

    sf::communication::transport_type comm_transport_type;
    if (transport_type == "webrtc") {
        comm_transport_type = sf::communication::transport_type::webrtc;
    } else if (transport_type == "rtp") {
        comm_transport_type = sf::communication::transport_type::udp;
    } else {
        sf::error(rtp_utils, "Unsupported transport type: %s", transport_type.c_str());
        return false;
    }

    const auto manager = sf::communication::create_communication_manager(comm_transport_type);
    if (!manager) {
        sf::error(rtp_utils, "Failed to create communication manager for disconnect");
        return false;
    }

    if (comm_transport_type == sf::communication::transport_type::webrtc) {
        if (webrtc_signaling_url.empty()) {
            sf::error(rtp_utils, "WebRTC transport requires signaling_url for disconnect");
            return false;
        }
        sf::communication::webrtc_config webrtc_cfg{};
        webrtc_cfg.signaling_url = webrtc_signaling_url;
        webrtc_cfg.ice_timeout_ms = 10000;
        sf::communication::manager_set_webrtc_config(manager.get(), webrtc_cfg);
    }

    const sf::communication::transport_config config{
            .host = server_host, .port = server_port, .timeout = std::chrono::milliseconds(5000)};
    const sf::communication::transport_error init_error = sf::communication::manager_initialize(manager.get(), config);
    if (init_error != sf::communication::transport_error::success) {
        sf::error(rtp_utils, "Failed to initialize communication manager for disconnect: %d",
                  static_cast<int>(init_error));
        return false;
    }

    const sf::communication::transport_error send_error =
            sf::communication::manager_send_disconnect(manager.get(), streaming_port, session_id);
    if (send_error != sf::communication::transport_error::success) {
        sf::warning(rtp_utils, "Failed to send disconnect request: %d", static_cast<int>(send_error));
        [[maybe_unused]] const sf::communication::transport_error shutdown_error =
                sf::communication::manager_shutdown(manager.get());
        return false;
    }

    const sf::communication::transport_error ack_error =
            sf::communication::manager_wait_for_disconnect_ack(manager.get(), streaming_port, session_id);
    if (ack_error != sf::communication::transport_error::success) {
        sf::warning(rtp_utils, "Disconnect ack not received: %d", static_cast<int>(ack_error));
        [[maybe_unused]] const sf::communication::transport_error shutdown_error =
                sf::communication::manager_shutdown(manager.get());
        return false;
    }

    [[maybe_unused]] const sf::communication::transport_error shutdown_error =
            sf::communication::manager_shutdown(manager.get());
    sf::info(rtp_utils, "Disconnect ack received for port %u", streaming_port);
    return true;
}
