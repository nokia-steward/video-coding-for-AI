// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "signaling_client_internal.h"

#include <gio/gio.h>
#include <gst/gst.h>

#include <optional>
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
         * @brief Parses a WebSocket URL and extracts host and port.
         * @param url WebSocket URL in the form ws://host:port or wss://host:port.
         * @return Host and port if parsing succeeds; std::nullopt otherwise.
         */
        [[nodiscard]] std::optional<std::pair<std::string, guint>> parse_signaling_url(const std::string &url) {
            std::string remaining_url = url;
            if (remaining_url.find("ws://") == 0) {
                remaining_url = remaining_url.substr(5);
            } else if (remaining_url.find("wss://") == 0) {
                remaining_url = remaining_url.substr(6);
            } else {
                error(signaling, "Signaling URL must start with ws:// or wss://");
                return std::nullopt;
            }

            const size_t colon_position = remaining_url.find(':');
            if (colon_position == std::string::npos) {
                error(signaling, "Signaling URL missing port");
                return std::nullopt;
            }

            const std::string host = remaining_url.substr(0, colon_position);
            const std::string port_string = remaining_url.substr(colon_position + 1);
            const auto port = static_cast<guint>(std::stoi(port_string));
            return std::make_pair(host, port);
        }

        /**
         * @brief Creates and connects a TCP socket.
         * @param server_address Server socket address.
         * @param host Server host string for logging.
         * @param port Server port for logging.
         * @return Connected socket on success, nullptr on failure.
         */
        [[nodiscard]] GSocket *create_and_connect_socket(GSocketAddress *server_address, const std::string &host,
                                                         guint port) {
            GError *error_state = nullptr;
            GSocket *socket =
                    g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error_state);
            if (!socket) {
                error(signaling, "Failed to create socket: %s", error_state ? error_state->message : "Unknown error");
                g_clear_error(&error_state);
                return nullptr;
            }

            info(signaling, "Connecting to signaling server at %s:%u...", host.c_str(), port);
            if (!g_socket_connect(socket, server_address, nullptr, &error_state)) {
                error(signaling, "Failed to connect to signaling server: %s",
                      error_state ? error_state->message : "Unknown error");
                g_clear_error(&error_state);
                g_object_unref(socket);
                return nullptr;
            }

            info(signaling, "Successfully connected to signaling server");
            return socket;
        }

        /**
         * @brief Handles a received message payload.
         * @param client Active signaling client.
         * @param buffer Raw message bytes.
         * @param bytes_read Number of bytes read from socket.
         * @return true if receive loop should continue, false otherwise.
         */
        [[nodiscard]] bool handle_received_data(signaling_client *client, char *buffer, gssize bytes_read) {
            if (bytes_read < 0) {
                return false;
            }
            if (bytes_read == 0) {
                info(signaling, "Signaling server closed connection");
                return false;
            }

            buffer[bytes_read] = '\0';
            const std::string message_text(buffer, static_cast<size_t>(bytes_read));
            info(signaling, "Received raw message (%zd bytes): %s", bytes_read, message_text.c_str());

            const auto parsed_message = parse_signaling_message(message_text);
            if (!parsed_message) {
                warning(signaling, "Dropping malformed signaling message: %s", message_text.c_str());
                return true;
            }

            if (client->message_callback) {
                client->message_callback(*parsed_message);
            } else {
                error(signaling, "Message callback is null");
            }
            return true;
        }

        /**
         * @brief Blocking receive loop for incoming signaling messages.
         * @param client Active signaling client.
         */
        void signaling_receive_thread(signaling_client *client) {
            init_debug_category();
            info(signaling, "Signaling receive thread started");

            g_socket_set_blocking(client->socket, TRUE);
            g_socket_set_timeout(client->socket, 1);

            while (!client->should_stop && client->connected) {
                char buffer[4096];
                GError *error_state = nullptr;
                const gssize bytes_read =
                        g_socket_receive(client->socket, buffer, sizeof(buffer) - 1, nullptr, &error_state);

                if (bytes_read < 0) {
                    if (error_state && error_state->code == G_IO_ERROR_TIMED_OUT) {
                        g_clear_error(&error_state);
                        continue;
                    }
                    if (error_state && error_state->code == G_IO_ERROR_WOULD_BLOCK) {
                        g_clear_error(&error_state);
                        continue;
                    }
                    error(signaling, "Error reading from signaling socket: %s",
                          error_state ? error_state->message : "Unknown error");
                    g_clear_error(&error_state);
                    client->connected = false;
                    break;
                }

                if (!handle_received_data(client, buffer, bytes_read)) {
                    client->connected = false;
                    break;
                }
            }

            info(signaling, "Signaling receive thread exiting");
        }
    } // namespace

    std::unique_ptr<signaling_client, void (*)(signaling_client *)>
    create_signaling_client(const std::string &url, signaling_message_callback callback) {
        init_debug_category();
        auto client = std::unique_ptr<signaling_client, void (*)(signaling_client *)>(
                new signaling_client(url, std::move(callback)), destroy_signaling_client);
        info(signaling, "Created signaling client for URL: %s", url.c_str());
        return client;
    }

    void destroy_signaling_client(signaling_client *client) {
        if (!client) {
            return;
        }
        std::unique_ptr<signaling_client> owned_client(client);

        init_debug_category();
        if (owned_client->connected) {
            signaling_client_disconnect(owned_client.get());
        }
    }

    bool signaling_client_connect(signaling_client *client) {
        if (!client) {
            return false;
        }

        init_debug_category();
        if (client->connected) {
            warning(signaling, "Signaling client already connected");
            return true;
        }

        const auto parsed_url = parse_signaling_url(client->url);
        if (!parsed_url) {
            return false;
        }

        const auto &[host, port] = *parsed_url;
        GInetAddress *inet_address = g_inet_address_new_from_string(host.c_str());
        if (!inet_address) {
            error(signaling, "Invalid host address: %s", host.c_str());
            return false;
        }

        client->server_address = g_inet_socket_address_new(inet_address, port);
        g_object_unref(inet_address);

        if (!client->server_address) {
            error(signaling, "Failed to create server address");
            return false;
        }

        client->socket = create_and_connect_socket(client->server_address, host, port);
        if (!client->socket) {
            g_object_unref(client->server_address);
            client->server_address = nullptr;
            return false;
        }

        client->connected = true;
        client->should_stop = false;
        info(signaling, "Starting receive thread for signaling client...");
        client->receive_thread = std::make_unique<std::jthread>(signaling_receive_thread, client);
        info(signaling, "Signaling client connected to %s", client->url.c_str());
        return true;
    }

    bool signaling_client_send(signaling_client *client, const signaling_message &message) {
        if (!client || !client->connected || !client->socket) {
            error(signaling, "Cannot send: client=%p, connected=%d, socket=%p", static_cast<void *>(client),
                  client ? client->connected : false, client ? static_cast<void *>(client->socket) : nullptr);
            return false;
        }

        init_debug_category();
        const auto message_text = format_signaling_message(message);
        if (!message_text) {
            error(signaling, "Unknown signaling message type: %d", static_cast<int>(message.type));
            return false;
        }

        GError *error_state = nullptr;
        const gssize sent =
                g_socket_send(client->socket, message_text->c_str(), message_text->length(), nullptr, &error_state);
        if (sent < 0 || static_cast<size_t>(sent) != message_text->length()) {
            error(signaling, "Failed to send signaling message: %s",
                  error_state ? error_state->message : "Unknown error");
            g_clear_error(&error_state);
            return false;
        }

        info(signaling, "Successfully sent %zd bytes to signaling server", sent);
        return true;
    }

    void signaling_client_disconnect(signaling_client *client) {
        if (!client) {
            return;
        }

        init_debug_category();
        info(signaling, "Disconnecting signaling client...");

        client->should_stop = true;
        client->connected = false;

        if (client->receive_thread && client->receive_thread->joinable()) {
            client->receive_thread->join();
            client->receive_thread.reset();
        }

        if (client->io_watch_id > 0) {
            g_source_remove(client->io_watch_id);
            client->io_watch_id = 0;
        }

        if (client->io_channel) {
            g_io_channel_unref(client->io_channel);
            client->io_channel = nullptr;
        }

        if (client->socket) {
            g_socket_close(client->socket, nullptr);
            g_object_unref(client->socket);
            client->socket = nullptr;
        }

        if (client->server_address) {
            g_object_unref(client->server_address);
            client->server_address = nullptr;
        }

        info(signaling, "Signaling client disconnected");
    }

    bool signaling_client_is_connected(const signaling_client *client) {
        if (!client) {
            return false;
        }
        return client->connected;
    }
} // namespace sf::communication
