// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "signaling_server_internal.h"

#include <gio/gio.h>
#include <gst/gst.h>

#include <cstdint>
#include <sstream>
#include <utility>

#include "../../include/logging.h"
#include "signaling_message_codec.h"

namespace sf::communication {
    namespace {
        GST_DEBUG_CATEGORY_STATIC(signaling);
#define GST_CAT_DEFAULT signaling

        /**
         * @brief Initializes the signaling debug category once per process.
         */
        void init_debug_category() {
            static bool initialized = false;
            if (!initialized) {
                GST_DEBUG_CATEGORY_INIT(signaling, "signaling", 0, "WebRTC signaling");
                initialized = true;
            }
        }

        /**
         * @brief Finds the client identifier associated with an IO channel.
         * @param server Signaling server instance.
         * @param source IO channel pointer.
         * @return Matching client identifier or an empty string.
         */
        [[nodiscard]] std::string find_client_id_by_channel(signaling_server *server, GIOChannel *source) {
            for (const auto &[client_id, channel]: server->client_channels) {
                if (channel == source) {
                    return client_id;
                }
            }
            return "";
        }

        /**
         * @brief Reads raw bytes from an IO channel.
         * @param source IO channel to read from.
         * @param buffer Destination buffer.
         * @param buffer_size Destination buffer size.
         * @return Number of bytes read, or -1 on error.
         */
        [[nodiscard]] gssize read_from_io_channel(GIOChannel *source, char *buffer, size_t buffer_size) {
            gsize bytes_read = 0;
            GError *error_state = nullptr;
            const GIOStatus status =
                    g_io_channel_read_chars(source, buffer, buffer_size - 1, &bytes_read, &error_state);
            if (status == G_IO_STATUS_ERROR) {
                error(signaling, "Error reading from IO channel: %s",
                      error_state ? error_state->message : "Unknown error");
                g_clear_error(&error_state);
                return -1;
            }
            return static_cast<gssize>(bytes_read);
        }

        /**
         * @brief Parses and dispatches a client message.
         * @param server Signaling server instance.
         * @param client_id Client identifier.
         * @param message_text Raw message payload.
         * @return true if processing should continue, false otherwise.
         */
        [[nodiscard]] bool process_client_message(signaling_server *server, const std::string &client_id,
                                                  const std::string &message_text) {
            const auto parsed_message = parse_signaling_message(message_text);
            if (!parsed_message) {
                warning(signaling, "Dropping malformed signaling message from %s: %s", client_id.c_str(),
                        message_text.c_str());
                return true;
            }

            if (server->message_callback) {
                server->message_callback(client_id, *parsed_message);
            }
            return true;
        }

        /**
         * @brief Handles a readable event on a client IO channel.
         * @param server Signaling server instance.
         * @param client_id Client identifier.
         * @param source IO channel.
         * @return true to keep watch active; false to remove it.
         */
        [[nodiscard]] bool handle_incoming_data(signaling_server *server, const std::string &client_id,
                                                GIOChannel *source) {
            char buffer[4096];
            const gssize bytes_read = read_from_io_channel(source, buffer, sizeof(buffer));
            if (bytes_read < 0) {
                return false;
            }
            if (bytes_read == 0) {
                return true;
            }

            buffer[bytes_read] = '\0';
            const std::string message_text(buffer, static_cast<size_t>(bytes_read));
            return process_client_message(server, client_id, message_text);
        }

        /**
         * @brief Releases all resources associated with one connected client.
         * @param server Signaling server instance.
         * @param client_id Client identifier.
         */
        void cleanup_disconnected_client(signaling_server *server, const std::string &client_id) {
            if (server->client_watches.contains(client_id)) {
                g_source_remove(server->client_watches[client_id]);
                server->client_watches.erase(client_id);
            }

            if (server->client_channels.contains(client_id)) {
                g_io_channel_unref(server->client_channels[client_id]);
                server->client_channels.erase(client_id);
            }

            if (server->client_sockets.contains(client_id)) {
                g_object_unref(server->client_sockets[client_id]);
                server->client_sockets.erase(client_id);
            }

            if (server->client_connections.contains(client_id)) {
                g_object_unref(server->client_connections[client_id]);
                server->client_connections.erase(client_id);
            }
        }

        /**
         * @brief Handles a client disconnection event.
         * @param server Signaling server instance.
         * @param client_id Client identifier.
         * @return false so GLib removes the watch.
         */
        [[nodiscard]] bool handle_client_disconnection(signaling_server *server, const std::string &client_id) {
            if (server->client_disconnected_callback) {
                server->client_disconnected_callback(client_id);
            }

            cleanup_disconnected_client(server, client_id);
            info(signaling, "Client disconnected: %s", client_id.c_str());
            return false;
        }

        /**
         * @brief GLib callback for client channel events.
         */
        gboolean on_client_io_event(GIOChannel *source, GIOCondition condition, gpointer user_data) {
            auto *server = static_cast<signaling_server *>(user_data);
            init_debug_category();

            const std::string client_id = find_client_id_by_channel(server, source);
            if (client_id.empty()) {
                return FALSE;
            }

            if ((condition & G_IO_IN) && !handle_incoming_data(server, client_id, source)) {
                return FALSE;
            }

            if (condition & (G_IO_HUP | G_IO_ERR)) {
                return handle_client_disconnection(server, client_id) ? TRUE : FALSE;
            }

            return TRUE;
        }

        /**
         * @brief Registers a newly accepted client connection.
         * @param server Signaling server instance.
         * @param connection GLib socket connection.
         * @return Generated client identifier, or empty string on failure.
         */
        [[nodiscard]] std::string setup_client_connection(signaling_server *server, GSocketConnection *connection) {
            if (!connection) {
                error(signaling, "Connection is nullptr");
                return "";
            }
            if (!G_IS_SOCKET_CONNECTION(connection)) {
                error(signaling, "Invalid connection object type");
                return "";
            }

            g_object_ref(connection);
            const std::string client_id = "client_" + std::to_string(reinterpret_cast<uintptr_t>(connection));

            GSocket *socket = g_socket_connection_get_socket(connection);
            if (!socket) {
                error(signaling, "Failed to get socket from connection");
                g_object_unref(connection);
                return "";
            }

            server->client_connections[client_id] = connection;
            g_object_ref(socket);
            server->client_sockets[client_id] = socket;

            GIOChannel *io_channel = g_io_channel_unix_new(g_socket_get_fd(socket));
            g_io_channel_set_encoding(io_channel, nullptr, nullptr);
            g_io_channel_set_buffered(io_channel, FALSE);
            server->client_channels[client_id] = io_channel;

            const guint watch_id = g_io_add_watch(io_channel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
                                                  on_client_io_event, server);
            server->client_watches[client_id] = watch_id;
            return client_id;
        }

        /**
         * @brief GLib callback for incoming TCP connections.
         */
        gboolean on_incoming_connection(GSocketService *service, GSocketConnection *connection, GObject *source_object,
                                        gpointer user_data) {
            (void) service;
            (void) source_object;
            auto *server = static_cast<signaling_server *>(user_data);
            init_debug_category();

            if (!connection) {
                error(signaling, "Received nullptr connection in incoming callback");
                return FALSE;
            }

            const std::string client_id = setup_client_connection(server, connection);
            if (client_id.empty()) {
                return FALSE;
            }

            if (server->client_connected_callback) {
                server->client_connected_callback(client_id);
            }

            info(signaling, "New client connected: %s", client_id.c_str());
            return TRUE;
        }
    } // namespace

    std::unique_ptr<signaling_server, void (*)(signaling_server *)>
    create_signaling_server(uint16_t port, bool use_ssl, signaling_client_connected_callback client_connected_callback,
                            signaling_client_disconnected_callback client_disconnected_callback,
                            signaling_server_message_callback message_callback) {
        init_debug_category();
        auto server = std::unique_ptr<signaling_server, void (*)(signaling_server *)>(
                new signaling_server(port, use_ssl, std::move(client_connected_callback),
                                     std::move(client_disconnected_callback), std::move(message_callback)),
                destroy_signaling_server);
        info(signaling, "Created signaling server for port %u (SSL: %s)", port, use_ssl ? "yes" : "no");
        return server;
    }

    void destroy_signaling_server(signaling_server *server) {
        if (!server) {
            return;
        }
        std::unique_ptr<signaling_server> owned_server(server);

        init_debug_category();
        if (owned_server->service) {
            signaling_server_stop(owned_server.get());
        }
    }

    bool signaling_server_start(signaling_server *server) {
        if (!server) {
            return false;
        }

        init_debug_category();
        if (server->listener) {
            warning(signaling, "Signaling server already started");
            return true;
        }

        GError *error_state = nullptr;
        server->service = g_socket_service_new();

        GInetAddress *address = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
        GSocketAddress *socket_address = g_inet_socket_address_new(address, server->port);
        g_object_unref(address);

        if (!g_socket_listener_add_address(G_SOCKET_LISTENER(server->service), socket_address, G_SOCKET_TYPE_STREAM,
                                           G_SOCKET_PROTOCOL_TCP, nullptr, nullptr, &error_state)) {
            error(signaling, "Failed to bind signaling server to port %u: %s", server->port,
                  error_state ? error_state->message : "Unknown error");
            g_clear_error(&error_state);
            g_object_unref(socket_address);
            g_object_unref(server->service);
            server->service = nullptr;
            return false;
        }

        g_object_unref(socket_address);
        g_signal_connect(server->service, "incoming", G_CALLBACK(on_incoming_connection), server);
        g_socket_service_start(server->service);
        info(signaling, "Signaling server started on port %u", server->port);
        return true;
    }

    void signaling_server_stop(signaling_server *server) {
        if (!server || !server->service) {
            return;
        }

        init_debug_category();
        for (const auto &[client_id, watch_id]: server->client_watches) {
            (void) client_id;
            g_source_remove(watch_id);
        }
        server->client_watches.clear();

        for (const auto &[client_id, channel]: server->client_channels) {
            (void) client_id;
            g_io_channel_unref(channel);
        }
        server->client_channels.clear();

        for (const auto &[client_id, socket]: server->client_sockets) {
            (void) client_id;
            g_socket_close(socket, nullptr);
            g_object_unref(socket);
        }
        server->client_sockets.clear();

        for (const auto &[client_id, connection]: server->client_connections) {
            (void) client_id;
            g_object_unref(connection);
        }
        server->client_connections.clear();

        g_object_unref(server->service);
        server->service = nullptr;
        info(signaling, "Signaling server stopped");
    }

    bool signaling_server_send_to_client(signaling_server *server, const std::string &client_id,
                                         const signaling_message &message) {
        if (!server) {
            return false;
        }

        init_debug_category();
        const auto socket_iterator = server->client_sockets.find(client_id);
        if (socket_iterator == server->client_sockets.end()) {
            error(signaling, "Client not found: %s", client_id.c_str());
            return false;
        }

        const auto message_text = format_signaling_message(message);
        if (!message_text) {
            error(signaling, "Unknown signaling message type: %d", static_cast<int>(message.type));
            return false;
        }

        GError *error_state = nullptr;
        const gssize sent = g_socket_send(socket_iterator->second, message_text->c_str(), message_text->length(),
                                          nullptr, &error_state);
        if (sent < 0 || static_cast<size_t>(sent) != message_text->length()) {
            error(signaling, "Failed to send signaling message to client %s: %s", client_id.c_str(),
                  error_state ? error_state->message : "Unknown error");
            g_clear_error(&error_state);
            return false;
        }

        info(signaling, "Successfully sent %zd bytes to client %s", sent, client_id.c_str());
        return true;
    }
} // namespace sf::communication
