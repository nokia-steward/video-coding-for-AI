// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/communication/transport_udp.h"
#include "../../include/communication/transport.h"

#include <gio/gio.h>
#include <gst/gst.h>
#include <memory>
#include <optional>
#include "../../include/logging.h"

namespace sf::communication {
    namespace {
        // Debug category for UDP transport implementation.
        GST_DEBUG_CATEGORY_STATIC(transport_udp);
#define GST_CAT_DEFAULT transport_udp

        /**
         * @brief Initialize the UDP transport debug category once.
         */
        void init_debug_category() {
            static bool initialized = false;
            if (!initialized) {
                GST_DEBUG_CATEGORY_INIT(transport_udp, "transport-udp", 0, "UDP transport implementation");
                initialized = true;
            }
        }
    } // namespace

    /**
     * @struct udp_transport_data
     * @brief Holds socket state for UDP transport connections.
     */
    struct udp_transport_data {
        GSocket *socket;
        GSocketAddress *server_address;
        bool connected;

        /**
         * @brief Construct an unconnected UDP transport state.
         */
        udp_transport_data() : socket(nullptr), server_address(nullptr), connected(false) {}
    };

    udp_transport_data_ptr create_udp_transport_data() {
        init_debug_category();
        return udp_transport_data_ptr(new udp_transport_data{}, destroy_udp_transport_data);
    }

    void destroy_udp_transport_data(udp_transport_data *data) {
        if (!data) {
            return;
        }

        if (data->socket) {
            g_object_unref(data->socket);
            data->socket = nullptr;
        }

        if (data->server_address) {
            g_object_unref(data->server_address);
            data->server_address = nullptr;
        }

        std::unique_ptr<udp_transport_data>{data};
    }

    transport_error udp_connect_impl(udp_transport_data *data, const transport_config &config) {
        if (!data) {
            return transport_error::unknown_error;
        }

        init_debug_category();

        // Clean up any existing connection
        if (data->socket) {
            g_object_unref(data->socket);
            data->socket = nullptr;
        }
        if (data->server_address) {
            g_object_unref(data->server_address);
            data->server_address = nullptr;
        }
        data->connected = false;

        // Create server address
        GInetAddress *inet_address = g_inet_address_new_from_string(config.host.c_str());
        if (!inet_address) {
            error(transport_udp, "Invalid IP address: %s", config.host.c_str());
            return transport_error::connection_failed;
        }

        data->server_address = g_inet_socket_address_new(inet_address, config.port);
        g_object_unref(inet_address);

        if (!data->server_address) {
            error(transport_udp, "Failed to create server address for %s:%u", config.host.c_str(), config.port);
            return transport_error::connection_failed;
        }

        // Create UDP socket
        GError *gerror = nullptr;
        data->socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &gerror);
        if (!data->socket) {
            error(transport_udp, "Error creating UDP socket: %s", gerror ? gerror->message : "Unknown error");
            g_clear_error(&gerror);
            g_object_unref(data->server_address);
            data->server_address = nullptr;
            return transport_error::connection_failed;
        }

        // Set socket timeout
        const auto timeout_seconds = static_cast<guint>(config.timeout.count() / 1000);
        g_socket_set_timeout(data->socket, timeout_seconds > 0 ? timeout_seconds : 5);

        data->connected = true;
        info(transport_udp, "UDP transport connected to %s:%u", config.host.c_str(), config.port);

        return transport_error::success;
    }

    transport_error udp_send_impl(udp_transport_data *data, const protocol_message &message) {
        if (!data || !data->socket || !data->connected) {
            return transport_error::not_connected;
        }

        init_debug_category();

        // Serialize message
        const std::vector<uint8_t> serialized = serialize_message(message);

        // Send via UDP
        GError *gerror = nullptr;
        const gssize sent =
                g_socket_send_to(data->socket, data->server_address, reinterpret_cast<const gchar *>(serialized.data()),
                                 static_cast<gsize>(serialized.size()), nullptr, &gerror);

        if (sent < 0) {
            if (gerror && g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
                error(transport_udp, "UDP send timeout");
                g_clear_error(&gerror);
                return transport_error::timeout;
            }
            error(transport_udp, "Error sending UDP message: %s", gerror ? gerror->message : "Unknown error");
            g_clear_error(&gerror);
            return transport_error::send_failed;
        }

        if (static_cast<size_t>(sent) != serialized.size()) {
            warning(transport_udp, "Partial UDP send: %zd of %zu bytes", sent, serialized.size());
        }

        debug(transport_udp, "Sent %zu bytes via UDP", serialized.size());
        return transport_error::success;
    }

    namespace {
        /**
         * @brief Receives data from UDP socket.
         * @param socket UDP socket.
         * @param buffer Buffer to receive data into.
         * @param buffer_size Size of the buffer.
         * @return Number of bytes received, or negative value on error.
         */
        [[nodiscard]] gssize receive_udp_data(GSocket *socket, char *buffer, size_t buffer_size) {
            GError *gerror = nullptr;
            GSocketAddress *sender_address = nullptr;

            const gssize received =
                    g_socket_receive_from(socket, &sender_address, buffer, buffer_size, nullptr, &gerror);

            if (sender_address) {
                g_object_unref(sender_address);
            }

            if (received < 0) {
                if (gerror && g_error_matches(gerror, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
                    debug(transport_udp, "UDP receive timeout");
                    g_clear_error(&gerror);
                    return -1; // Signal timeout
                }
                error(transport_udp, "Error receiving UDP message: %s", gerror ? gerror->message : "Unknown error");
                g_clear_error(&gerror);
                return -2; // Signal error
            }

            return received;
        }

        /**
         * @brief Deserializes a protocol message from received UDP data.
         * @param buffer Buffer containing received data.
         * @param received_bytes Number of bytes received.
         * @return Deserialized message or empty optional on failure.
         */
        [[nodiscard]] std::optional<protocol_message> deserialize_udp_message(const char *buffer,
                                                                              gssize received_bytes) {
            if (received_bytes == 0) {
                warning(transport_udp, "Received empty UDP message");
                return std::nullopt;
            }

            const std::vector data_vec(reinterpret_cast<const uint8_t *>(buffer),
                                       reinterpret_cast<const uint8_t *>(buffer) + received_bytes);
            const auto message_opt = deserialize_message(data_vec);

            if (!message_opt) {
                error(transport_udp, "Failed to deserialize UDP message");
                return std::nullopt;
            }

            return message_opt;
        }
    } // namespace

    transport_receive_result udp_receive_impl(udp_transport_data *data) {
        if (!data || !data->socket || !data->connected) {
            return transport_receive_result::failure(transport_error::not_connected);
        }

        init_debug_category();

        // Receive from UDP
        char buffer[4096];
        const gssize received = receive_udp_data(data->socket, buffer, sizeof(buffer));

        if (received < 0) {
            if (received == -1) {
                return transport_receive_result::failure(transport_error::timeout);
            }
            return transport_receive_result::failure(transport_error::receive_failed);
        }

        // Deserialize message
        const auto message_opt = deserialize_udp_message(buffer, received);
        if (!message_opt) {
            return transport_receive_result::failure(transport_error::invalid_message);
        }

        debug(transport_udp, "Received %zd bytes via UDP", received);
        return transport_receive_result::success(message_opt.value());
    }

    transport_error udp_disconnect_impl(udp_transport_data *data) {
        if (!data) {
            return transport_error::unknown_error;
        }

        init_debug_category();

        if (data->socket) {
            g_object_unref(data->socket);
            data->socket = nullptr;
        }

        if (data->server_address) {
            g_object_unref(data->server_address);
            data->server_address = nullptr;
        }

        data->connected = false;
        info(transport_udp, "UDP transport disconnected");

        return transport_error::success;
    }

    bool udp_is_connected_impl(const udp_transport_data *data) {
        if (!data) {
            return false;
        }
        return data->connected && data->socket != nullptr;
    }
} // namespace sf::communication
