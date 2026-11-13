// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "server.h"

#include <gio/gio.h>
#include <glib.h>
#include <gst/gst.h>
#include <thread>
#include <vector>

#include "../client/client_manager.h"
#include "../pipeline/pipeline.h"
#include "../transport/transport_factory.h"
#include "communication/protocol.h"
#include "communication/signaling.h"
#include "logging.h"

// Debug category for server lifecycle and client messaging.
GST_DEBUG_CATEGORY(server_category);
#define GST_CAT_DEFAULT server_category

/**
 * @brief Initialize the decoded frame processing debug category.
 */
void init_server_debug_category() { GST_DEBUG_CATEGORY_INIT(server_category, "server", 0, "Server"); }

namespace {
    struct protocol_dispatch_callbacks {
        std::function<void(uint16_t, uint32_t)> on_heartbeat;
        std::function<std::optional<uint16_t>()> allocate_handshake_port;
        std::function<bool(sf::communication::message_type, const std::vector<uint8_t> &)> send_response;
        std::function<bool(uint16_t, uint32_t, sf::communication::protocol_version)> on_handshake_success;
        std::function<void(uint16_t)> on_disconnect;
    };

    [[nodiscard]] bool dispatch_protocol_message(const sf::communication::protocol_message &message,
                                                 const protocol_dispatch_callbacks &callbacks) {
        if (message.type == sf::communication::message_type::heartbeat) {
            const auto heartbeat_opt = sf::communication::deserialize_heartbeat(message);
            if (!heartbeat_opt) {
                sf::warning(server_category, "Failed to deserialize heartbeat message");
                return true;
            }

            callbacks.on_heartbeat(heartbeat_opt->streaming_port, heartbeat_opt->session_id);
            return true;
        }

        if (message.type == sf::communication::message_type::disconnect_request) {
            const auto disconnect_opt = sf::communication::deserialize_disconnect_request(message);
            if (!disconnect_opt) {
                sf::warning(server_category, "Failed to deserialize disconnect request");
                return true;
            }

            const auto response_message = sf::communication::serialize_disconnect_ack(*disconnect_opt, message.version);
            const auto response_data = sf::communication::serialize_message(response_message);
            if (!callbacks.send_response(sf::communication::message_type::disconnect_request, response_data)) {
                sf::warning(server_category, "Failed to send disconnect acknowledgment");
            }

            callbacks.on_disconnect(disconnect_opt->streaming_port);
            return true;
        }

        if (message.type == sf::communication::message_type::handshake_request) {
            const auto request_opt = sf::communication::deserialize_handshake_request(message);
            if (!request_opt) {
                sf::warning(server_category, "Failed to deserialize handshake request");
                return true;
            }

            const auto port_opt = callbacks.allocate_handshake_port();
            if (!port_opt.has_value()) {
                sf::warning(server_category, "Unable to allocate handshake response port");
                return true;
            }

            const uint16_t streaming_port = port_opt.value();
            const auto session_id = static_cast<uint32_t>(streaming_port);
            const sf::communication::handshake_response response{streaming_port, session_id};
            const auto response_message = sf::communication::serialize_handshake_response(response, message.version);
            const auto response_data = sf::communication::serialize_message(response_message);
            if (callbacks.send_response(sf::communication::message_type::handshake_request, response_data)) {
                if (!callbacks.on_handshake_success(streaming_port, session_id, message.version)) {
                    sf::warning(server_category,
                                "Handshake succeeded but post-handshake setup failed for port %u session_id %u",
                                streaming_port, session_id);
                }
            } else {
                sf::warning(server_category, "Failed to send handshake response");
            }
            return true;
        }

        return false;
    }
} // namespace

rtp_server::rtp_server(const guint16 port, const rtp_monitoring_config &monitoring_config,
                       const rtp_output_config &rtp_output_config_param,
                       const std::optional<std::string> &output_directory_param,
                       const sf::background_color background_color_param, const transport_type transport_type_param,
                       const bool draw_bitrate_param, const bool draw_fps_param,
                       const rtp_server_config *full_config_param) :
    handshake_port(port),
    next_client_port(5002), main_loop(nullptr), socket(nullptr), listen_address(nullptr), socket_source(nullptr),
    client_manager_ptr(nullptr), config(monitoring_config), rtp_output(rtp_output_config_param),
    output_directory(output_directory_param), background_color(background_color_param),
    draw_bitrate(draw_bitrate_param), draw_fps(draw_fps_param),
    shutdown_initiated(false), transport_ctx{.functions = {}},
    signaling_server_ptr(nullptr, sf::communication::destroy_signaling_server),
    communication_transport_type_enum(transport_type_param),
    webrtc_config_data(full_config_param && transport_type_param == transport_type::webrtc
                               ? std::make_optional(*full_config_param)
                               : std::nullopt),
    full_config_ptr(full_config_param), webrtc_client_to_port(), webrtc_port_to_client() {}

void rtp_server::set_signaling_server(
        std::unique_ptr<sf::communication::signaling_server, void (*)(sf::communication::signaling_server *)> server) {
    signaling_server_ptr = std::move(server);
}

sf::communication::signaling_server *rtp_server::get_signaling_server() const { return signaling_server_ptr.get(); }

rtp_server::~rtp_server() {
    // First ensure main loop is stopped
    if (main_loop) {
        g_main_loop_quit(main_loop.get());
    }

    // Stop signaling server if running
    if (signaling_server_ptr) {
        sf::communication::signaling_server_stop(signaling_server_ptr.get());
    }

    // Write test mode summaries before destroying pipelines, so results are
    // persisted even when shutdown is triggered by SIGINT rather than an
    // explicit client disconnect.
    try {
        flush_test_mode_summaries();
    } catch (const std::exception &e) {
        sf::error(server_category, "Exception flushing test mode summaries: %s", e.what());
    }

    // Clean up client manager (which will clean up all pipelines)
    if (client_manager_ptr) {
        sf::info(server_category, "Cleaning up client manager...");
        try {
            client_manager_ptr.reset();
        } catch (const std::exception &e) {
            sf::error(server_category, "Exception cleaning up client manager: %s", e.what());
        }
    }

    // Clean up main loop
    if (main_loop) {
        main_loop.reset();
    }
}

bool rtp_server::initialize() {
    sf::info(server_category, "Initializing RTP server on port %u (transport: %s)...", handshake_port,
             transport_type_to_string(communication_transport_type_enum));

    // Create main loop
    main_loop = sf::unique_g_main_loop_ptr(g_main_loop_new(nullptr, FALSE));
    if (!main_loop) {
        sf::error(server_category, "Failed to create main loop");
        return false;
    }

    // Create transport context
    transport_ctx = create_transport_context(communication_transport_type_enum,
                                             webrtc_config_data.has_value() ? &(*webrtc_config_data) : nullptr);

    // Delegate initialization to transport functions
    if (!transport_ctx.functions.initialize_server(this)) {
        sf::error(server_category, "Failed to initialize transport");
        free_transport_context(&transport_ctx);
        cleanup();
        return false;
    }

    // Create client manager with transport context
    const bool draw_bitrate_large_value = full_config_ptr ? full_config_ptr->draw_bitrate_large : false;
    client_manager_ptr = std::make_unique<client_manager>(main_loop.get(), config, rtp_output, output_directory,
                                                          background_color, transport_ctx, draw_bitrate,
                                                          draw_bitrate_large_value, draw_fps, full_config_ptr);
    if (!client_manager_ptr) {
        sf::error(server_category, "Failed to create client manager");
        cleanup();
        return false;
    }

    sf::info(server_category, "RTP server initialized successfully on port %u", handshake_port);
    return true;
}

int rtp_server::run() const {
    if (!main_loop) {
        sf::error(server_category, "Server not initialized");
        return -1;
    }

    if (communication_transport_type_enum == transport_type::webrtc) {
        sf::info(server_category, "Listening for WebRTC clients via signaling server on port %u...",
                 webrtc_config_data ? webrtc_config_data->webrtc_signaling_port : 0);
    } else {
        sf::info(server_category, "Listening for new clients on RTP input handshake port %u...", handshake_port);
    }

    // Run the main loop
    g_main_loop_run(main_loop.get());

    sf::info(server_category, "RTP server main loop ended");
    return 0;
}

void rtp_server::stop() const {
    if (shutdown_initiated.exchange(true)) {
        // Already shutting down
        return;
    }

    sf::info(server_category, "Stopping RTP server...");

    if (main_loop) {
        g_main_loop_quit(main_loop.get());
    }
}

void rtp_server::cleanup() {
    sf::info(server_category, "Cleaning up RTP server resources...");

    // Stop session monitoring first to prevent race conditions during cleanup
    // The session_monitor runs in a background thread and could try to clean up
    // clients while we're already stopping them
    if (client_manager_ptr) {
        client_manager_ptr->get_session_monitor().stop_monitoring();
    }

    // Stop all client pipelines
    if (client_manager_ptr) {
        auto &pipelines = client_manager_ptr->get_client_pipelines();
        for (auto &[port, pipeline]: pipelines) {
            sf::info(server_category, "Stopping pipeline for port %u during cleanup", port);
            pipeline->stop_pipeline();
        }
    }

    // Write test mode summaries after pipelines are stopped but before they are
    // destroyed. At this point processing threads have finished, so all probe
    // results are finalized in test_mode_ctx.
    try {
        flush_test_mode_summaries();
    } catch (const std::exception &e) {
        sf::error(server_category, "Exception flushing test mode summaries during cleanup: %s", e.what());
    }

    // Stop signaling server if running
    if (signaling_server_ptr) {
        sf::communication::signaling_server_stop(signaling_server_ptr.get());
    }

    // Clean up socket resources
    if (socket_source) {
        g_source_destroy(socket_source.get());
        socket_source.reset();
    }

    if (socket) {
        socket.reset();
    }

    if (listen_address) {
        listen_address.reset();
    }

    // Cleanup transport context (std::function handles cleanup automatically)
    free_transport_context(&transport_ctx);

    sf::info(server_category, "RTP server cleanup completed");
}

bool rtp_server::create_handshake_socket() {
    GError *error = nullptr;

    // Create socket for RTP input handshake transport
    socket = sf::unique_gsocket_ptr(
            g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &error));
    if (!socket) {
        sf::error(server_category, "Failed to create RTP input handshake socket: %s", error->message);
        g_clear_error(&error);
        return false;
    }

    // Create address
    listen_address = sf::unique_g_socket_address_ptr(
            g_inet_socket_address_new(g_inet_address_new_any(G_SOCKET_FAMILY_IPV4), handshake_port));
    if (!listen_address) {
        sf::error(server_category, "Failed to create socket address");
        return false;
    }

    // Bind socket
    if (!g_socket_bind(socket.get(), listen_address.get(), true, &error)) {
        sf::error(server_category, "Failed to bind socket to port %u: %s", handshake_port, error->message);
        g_clear_error(&error);
        return false;
    }

    sf::info(server_category, "RTP input handshake socket created and bound to port %u", handshake_port);
    return true;
}

void rtp_server::setup_socket_watcher() {
    // Create a cross-platform GSource to watch the socket for incoming messages
    socket_source = sf::unique_g_source_ptr(g_socket_create_source(socket.get(), G_IO_IN, nullptr));
    if (!socket_source) {
        sf::error(server_category, "Failed to create socket source");
        return;
    }

    sf::info(server_category, "Socket source created successfully, setting up callback...");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    g_source_set_callback(socket_source.get(), reinterpret_cast<GSourceFunc>(on_client_message), this, nullptr);
#pragma GCC diagnostic pop
    g_source_attach(socket_source.get(), g_main_loop_get_context(main_loop.get()));
}

gboolean rtp_server::on_client_message(GSocket *socket, [[maybe_unused]] GIOCondition condition, gpointer user_data) {
    auto *server = static_cast<rtp_server *>(user_data);

    gchar buffer[1024];
    GError *error = nullptr;
    GSocketAddress *client_address = nullptr;

    const gssize size = g_socket_receive_from(socket, &client_address, buffer, sizeof(buffer), nullptr, &error);
    if (size < 0) {
        sf::error(server_category, "Error receiving data: %s", error->message);
        g_clear_error(&error);
        return TRUE;
    }

    if (!client_address) {
        sf::warning(server_category, "Received data but client_address is null");
        return TRUE;
    }

    // Try to parse as binary protocol message first
    const std::vector<uint8_t> data_vec(reinterpret_cast<const uint8_t *>(buffer),
                                        reinterpret_cast<const uint8_t *>(buffer) + size);
    const auto message_opt = sf::communication::deserialize_message(data_vec);

    if (message_opt) {
        const auto handled = dispatch_protocol_message(
                message_opt.value(),
                {.on_heartbeat =
                         [server](const uint16_t streaming_port, const uint32_t session_id) {
                             if (!server->client_manager_ptr) {
                                 return;
                             }
                             auto &client_sessions = server->client_manager_ptr->get_client_sessions();
                             if (const auto it = client_sessions.find(streaming_port); it != client_sessions.end()) {
                                 it->second->update_activity();
                                 sf::info(server_category,
                                          "Heartbeat received for port %u session_id %u, activity updated",
                                          streaming_port, session_id);
                             } else {
                                 sf::warning(
                                         server_category,
                                         "Heartbeat received for unknown port %u (session may have been cleaned up)",
                                         streaming_port);
                             }
                         },
                 .allocate_handshake_port = [server]() -> std::optional<uint16_t> {
                     return server->allocate_client_port();
                 },
                 .send_response = [&socket, &client_address,
                                   &error]([[maybe_unused]] const sf::communication::message_type type,
                                           const std::vector<uint8_t> &response_data) -> bool {
                     g_socket_send_to(socket, client_address, reinterpret_cast<const gchar *>(response_data.data()),
                                      response_data.size(), nullptr, &error);
                     if (error) {
                         sf::error(server_category, "Error sending protocol response to client: %s", error->message);
                         g_clear_error(&error);
                         return false;
                     }
                     return true;
                 },
                 .on_handshake_success = [server, &socket, &client_address,
                                          &error](const uint16_t port, const uint32_t session_id,
                                                  const sf::communication::protocol_version version) -> bool {
                     if (!server->create_client_pipeline(port)) {
                         sf::error(server_category, "Failed to create client pipeline for port %u", port);
                         return false;
                     }

                     const sf::communication::stream_ready_message ready{port, session_id};
                     const auto ready_message = sf::communication::serialize_stream_ready(ready, version);
                     const auto ready_data = sf::communication::serialize_message(ready_message);
                     g_socket_send_to(socket, client_address, reinterpret_cast<const gchar *>(ready_data.data()),
                                      ready_data.size(), nullptr, &error);
                     if (error) {
                         sf::error(server_category, "Error sending stream-ready response to client: %s",
                                   error->message);
                         g_clear_error(&error);
                         return false;
                     }

                     sf::info(server_category, "Stream-ready sent for port %u session_id %u", port, session_id);
                     return true;
                 },
                 .on_disconnect = [server](const uint16_t port) { server->cleanup_client_by_port(port); }});
        if (handled) {
            g_object_unref(client_address);
            return TRUE;
        }

        sf::debug(server_category, "Received unknown protocol message type: %d", static_cast<int>(message_opt->type));
        g_object_unref(client_address);
        return TRUE;
    }

    // Fallback: Try to parse as old text-based protocol for backward compatibility
    buffer[size] = '\0';
    const std::string message(buffer);

    // Handle old text-based heartbeat messages
    if (message.find("RTP_HEARTBEAT:") == 0) {
        const std::string port_str = message.substr(14);
        try {
            const auto port = static_cast<guint16>(std::stoi(port_str));
            if (server->client_manager_ptr) {
                auto &client_sessions = server->client_manager_ptr->get_client_sessions();
                if (const auto it = client_sessions.find(port); it != client_sessions.end()) {
                    it->second->update_activity();
                    sf::info(server_category, "Heartbeat received (old protocol) for port %u, activity updated", port);
                }
            }
        } catch ([[maybe_unused]] const std::exception &e) {
            sf::warning(server_category, "Invalid heartbeat message format: %s", message.c_str());
        }
        g_object_unref(client_address);
        return TRUE;
    }

    // Handle old text-based handshake
    if (message == "RTP_HANDSHAKE") {
        sf::info(server_category, "Received old text-based RTP handshake from client");
        const guint16 new_port = server->allocate_client_port();
        const std::string reply_message = std::to_string(static_cast<unsigned int>(new_port));
        g_socket_send_to(socket, client_address, reply_message.c_str(), reply_message.length(), nullptr, &error);
        if (!error) {
            if (!server->create_client_pipeline(new_port)) {
                sf::error(server_category, "Failed to create client pipeline for port %u", new_port);
            }
        } else {
            g_clear_error(&error);
        }
        g_object_unref(client_address);
        return TRUE;
    }

    // Unknown message format
    sf::debug(server_category, "Received unknown message format (size: %zd)", size);
    g_object_unref(client_address);
    return TRUE; // Keep watching
}

bool rtp_server::create_client_pipeline(const guint16 port) const {
    sf::info(server_category, "Creating pipeline for port %u via client manager...", port);

    if (!client_manager_ptr) {
        sf::error(server_category, "Client manager not initialized");
        return false;
    }

    // Create pipeline via client manager
    if (!client_manager_ptr->create_client_pipeline(port)) {
        sf::error(server_category, "Client manager failed to create pipeline for port %u", port);
        return false;
    }

    // If WebRTC, setup webrtcbin signals after pipeline is created
    // Note: This is a const member function, but we need to access mutable maps
    if (communication_transport_type_enum == transport_type::webrtc && signaling_server_ptr) {
        if (const auto it = webrtc_port_to_client.find(port); it != webrtc_port_to_client.end()) {
            const std::string &client_id = it->second;
            auto &pipelines = client_manager_ptr->get_client_pipelines();
            if (const auto pipeline_it = pipelines.find(port); pipeline_it != pipelines.end()) {
                rtp_pipeline *pipeline = pipeline_it->second.get();
                if (transport_ctx.functions.setup_signals) {
                    if (!transport_ctx.functions.setup_signals(&pipeline->transport_build_ctx,
                                                               signaling_server_ptr.get(), &client_id)) {
                        sf::error(server_category, "Failed to setup transport signals for port %u", port);
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

void rtp_server::cleanup_client_by_port(const guint16 port) {
    if (!client_manager_ptr) {
        return;
    }

    sf::info(server_category, "Cleaning up client resources for port %u", port);

    auto &pipelines = client_manager_ptr->get_client_pipelines();
    if (const auto pipeline_it = pipelines.find(port); pipeline_it != pipelines.end()) {
        rtp_pipeline *pipeline = pipeline_it->second.get();
        pipeline->stop_pipeline_graceful();
        if (pipeline->test_mode_ctx) {
            const auto summary = build_test_mode_run_summary(*pipeline->test_mode_ctx);
            const auto frame_results = build_test_mode_frame_results(*pipeline->test_mode_ctx);
            const auto semseg_summary = build_semseg_test_mode_run_summary(*pipeline->test_mode_ctx);
            const auto semseg_frame_results = build_semseg_test_mode_frame_results(*pipeline->test_mode_ctx);
            log_test_mode_run_summary(summary, port, server_category);
            log_semseg_test_mode_run_summary(semseg_summary, port, server_category);
            if (pipeline->server_config) {
                if (pipeline->server_config->test_mode_output_md.has_value()) {
                    const bool write_ok = write_test_mode_run_summary_markdown(
                            summary, frame_results, port, pipeline->server_config->test_mode_output_md.value(),
                            server_category);
                    if (!write_ok) {
                        sf::warning(server_category, "Test mode markdown summary write failed for port %u", port);
                    }
                }
                if (pipeline->server_config->test_mode_output_json.has_value()) {
                    const bool write_ok = write_test_mode_run_summary_json(
                            summary, frame_results, port, pipeline->server_config->test_mode_output_json.value(),
                            server_category);
                    if (!write_ok) {
                        sf::warning(server_category, "Test mode json summary write failed for port %u", port);
                    }
                }
                if (pipeline->server_config->semseg_output_md.has_value()) {
                    const bool write_ok = write_semseg_test_mode_run_summary_markdown(
                            semseg_summary, semseg_frame_results, port,
                            pipeline->server_config->semseg_output_md.value(), server_category);
                    if (!write_ok) {
                        sf::warning(server_category, "SemSeg test mode markdown summary write failed for port %u",
                                    port);
                    }
                }
                if (pipeline->server_config->semseg_output_json.has_value()) {
                    const bool write_ok = write_semseg_test_mode_run_summary_json(
                            semseg_summary, semseg_frame_results, port,
                            pipeline->server_config->semseg_output_json.value(), server_category);
                    if (!write_ok) {
                        sf::warning(server_category, "SemSeg test mode json summary write failed for port %u", port);
                    }
                }
            }
        }
        pipelines.erase(pipeline_it);
    }

    auto &sessions = client_manager_ptr->get_client_sessions();
    sessions.erase(port);

    if (const auto it = webrtc_port_to_client.find(port); it != webrtc_port_to_client.end()) {
        webrtc_client_to_port.erase(it->second);
        webrtc_port_to_client.erase(it);
    }
}

void rtp_server::flush_test_mode_summaries() {
    if (test_mode_summaries_flushed || !client_manager_ptr) {
        return;
    }
    test_mode_summaries_flushed = true;

    auto &pipelines = client_manager_ptr->get_client_pipelines();
    for (const auto &[port, pipeline_ptr]: pipelines) {
        if (!pipeline_ptr || !pipeline_ptr->test_mode_ctx) {
            continue;
        }

        sf::info(server_category, "Writing test mode summary for port %u during shutdown", port);

        const auto summary = build_test_mode_run_summary(*pipeline_ptr->test_mode_ctx);
        const auto frame_results = build_test_mode_frame_results(*pipeline_ptr->test_mode_ctx);
        const auto semseg_summary = build_semseg_test_mode_run_summary(*pipeline_ptr->test_mode_ctx);
        const auto semseg_frame_results = build_semseg_test_mode_frame_results(*pipeline_ptr->test_mode_ctx);
        log_test_mode_run_summary(summary, port, server_category);
        log_semseg_test_mode_run_summary(semseg_summary, port, server_category);

        if (!pipeline_ptr->server_config) {
            continue;
        }

        if (pipeline_ptr->server_config->test_mode_output_md.has_value()) {
            if (!write_test_mode_run_summary_markdown(summary, frame_results, port,
                                                      pipeline_ptr->server_config->test_mode_output_md.value(),
                                                      server_category)) {
                sf::warning(server_category, "Test mode markdown summary write failed for port %u during shutdown",
                            port);
            }
        }
        if (pipeline_ptr->server_config->test_mode_output_json.has_value()) {
            if (!write_test_mode_run_summary_json(summary, frame_results, port,
                                                  pipeline_ptr->server_config->test_mode_output_json.value(),
                                                  server_category)) {
                sf::warning(server_category, "Test mode JSON summary write failed for port %u during shutdown", port);
            }
        }
        if (pipeline_ptr->server_config->semseg_output_md.has_value()) {
            if (!write_semseg_test_mode_run_summary_markdown(semseg_summary, semseg_frame_results, port,
                                                             pipeline_ptr->server_config->semseg_output_md.value(),
                                                             server_category)) {
                sf::warning(server_category,
                            "SemSeg test mode markdown summary write failed for port %u during shutdown", port);
            }
        }
        if (pipeline_ptr->server_config->semseg_output_json.has_value()) {
            if (!write_semseg_test_mode_run_summary_json(semseg_summary, semseg_frame_results, port,
                                                         pipeline_ptr->server_config->semseg_output_json.value(),
                                                         server_category)) {
                sf::warning(server_category, "SemSeg test mode JSON summary write failed for port %u during shutdown",
                            port);
            }
        }
    }
}

guint16 rtp_server::allocate_client_port() {
    const guint16 port = next_client_port;
    next_client_port = static_cast<guint16>(next_client_port + 2); // Increment by 2 for RTCP
    return port;
}

void rtp_server::handle_webrtc_client_connected(const std::string &client_id) {
    sf::info(server_category, "WebRTC client connected: %s", client_id.c_str());
    // Allocate port for this client
    const guint16 new_port = allocate_client_port();
    // Store client_id -> port mapping for SDP/ICE routing
    webrtc_client_to_port[client_id] = new_port;
    webrtc_port_to_client[new_port] = client_id;
    // Create pipeline for this client
    if (!create_client_pipeline(new_port)) {
        sf::error(server_category, "Failed to create WebRTC client pipeline for port %u", new_port);
    }
}

void rtp_server::handle_webrtc_client_disconnected(const std::string &client_id) {
    sf::info(server_category, "WebRTC client disconnected: %s", client_id.c_str());
    // Find and clean up pipeline for this client
    if (const auto it = webrtc_client_to_port.find(client_id); it != webrtc_client_to_port.end()) {
        const guint16 port = it->second;
        if (client_manager_ptr) {
            auto &pipelines = client_manager_ptr->get_client_pipelines();
            if (const auto pipeline_it = pipelines.find(port); pipeline_it != pipelines.end()) {
                pipeline_it->second->stop_pipeline();
                pipelines.erase(pipeline_it);
            }
            auto &sessions = client_manager_ptr->get_client_sessions();
            sessions.erase(port);
        }
        webrtc_port_to_client.erase(port);
        webrtc_client_to_port.erase(it);
    }
}

void rtp_server::handle_webrtc_signaling_message_callback(const std::string &client_id,
                                                          const sf::communication::signaling_message &msg) {
    if (msg.type == sf::communication::signaling_message_type::handshake ||
        msg.type == sf::communication::signaling_message_type::heartbeat ||
        msg.type == sf::communication::signaling_message_type::disconnect) {
        const std::vector<uint8_t> msg_data(msg.data.begin(), msg.data.end());
        const auto protocol_message_opt = sf::communication::deserialize_message(msg_data);
        if (!protocol_message_opt) {
            sf::error(server_category, "Failed to deserialize protocol message from signaling data");
            return;
        }

        const auto handled = dispatch_protocol_message(
                protocol_message_opt.value(),
                {.on_heartbeat =
                         [this](const uint16_t streaming_port, const uint32_t session_id) {
                             if (!client_manager_ptr) {
                                 return;
                             }
                             auto &client_sessions = client_manager_ptr->get_client_sessions();
                             if (const auto it = client_sessions.find(streaming_port); it != client_sessions.end()) {
                                 it->second->update_activity();
                                 sf::info(server_category,
                                          "Heartbeat received for port %u session_id %u, activity updated",
                                          streaming_port, session_id);
                             } else {
                                 sf::warning(
                                         server_category,
                                         "Heartbeat received for unknown port %u (session may have been cleaned up)",
                                         streaming_port);
                             }
                         },
                 .allocate_handshake_port = [this, &client_id]() -> std::optional<uint16_t> {
                     if (const auto it = webrtc_client_to_port.find(client_id); it != webrtc_client_to_port.end()) {
                         return it->second;
                     }
                     sf::warning(server_category, "Received handshake for unknown client: %s", client_id.c_str());
                     return std::nullopt;
                 },
                 .send_response = [this, &client_id](const sf::communication::message_type type,
                                                     const std::vector<uint8_t> &response_data) -> bool {
                     if (!signaling_server_ptr) {
                         return false;
                     }
                     const auto signaling_type = type == sf::communication::message_type::handshake_request
                                                         ? sf::communication::signaling_message_type::handshake
                                                         : sf::communication::signaling_message_type::disconnect;
                     const sf::communication::signaling_message response_message{
                             signaling_type, std::string(response_data.begin(), response_data.end())};
                     return sf::communication::signaling_server_send_to_client(signaling_server_ptr.get(), client_id,
                                                                               response_message);
                 },
                 .on_handshake_success = [](const uint16_t, const uint32_t,
                                            const sf::communication::protocol_version) { return true; },
                 .on_disconnect = [this](const uint16_t streaming_port) { cleanup_client_by_port(streaming_port); }});
        if (handled) {
            return;
        }
    }

    // Route other messages (SDP/ICE) to pipeline via transport functions
    if (const auto it = webrtc_client_to_port.find(client_id); it != webrtc_client_to_port.end()) {
        const guint16 port = it->second;
        if (client_manager_ptr) {
            auto &pipelines = client_manager_ptr->get_client_pipelines();
            if (const auto pipeline_it = pipelines.find(port); pipeline_it != pipelines.end()) {
                rtp_pipeline *pipeline = pipeline_it->second.get();
                if (transport_ctx.functions.handle_signaling_message) {
                    transport_ctx.functions.handle_signaling_message(&pipeline->transport_build_ctx, &msg, &client_id,
                                                                     signaling_server_ptr.get());
                }
            } else {
                sf::warning(server_category, "Received signaling message for unknown pipeline (port %u)", port);
            }
        }
    } else {
        sf::warning(server_category, "Received signaling message for unknown client: %s", client_id.c_str());
    }
}
