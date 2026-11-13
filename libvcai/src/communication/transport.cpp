// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/communication/transport.h"
#include "../../include/communication/transport_udp.h"
#include "../../include/communication/transport_webrtc.h"

// Need access to set_webrtc_config
#include "../../include/communication/transport_webrtc.h"

#include <gst/gst.h>
#include "../../include/logging.h"

namespace sf::communication {
    namespace {
        // Debug category for transport abstraction.
        GST_DEBUG_CATEGORY_STATIC(transport);
#define GST_CAT_DEFAULT transport

        /**
         * @brief Initialize the transport debug category once.
         */
        void init_debug_category() {
            static bool initialized = false;
            if (!initialized) {
                GST_DEBUG_CATEGORY_INIT(transport, "transport", 0, "Transport layer");
                initialized = true;
            }
        }

        // Forward declarations for transport handle operations
        /**
         * @brief Connect the transport using the configured parameters.
         * @param handle Transport handle to connect.
         * @param config Transport configuration.
         * @return transport_error describing the outcome.
         */
        transport_error transport_connect_impl(transport_handle *handle, const transport_config &config);

        /**
         * @brief Send a protocol message over the transport.
         * @param handle Transport handle to use.
         * @param message Message to send.
         * @return transport_error describing the outcome.
         */
        transport_error transport_send_impl(transport_handle *handle, const protocol_message &message);

        /**
         * @brief Receive a protocol message from the transport.
         * @param handle Transport handle to use.
         * @return Result containing the received message or error state.
         */
        transport_receive_result transport_receive_impl(transport_handle *handle);

        /**
         * @brief Disconnect and clean up the transport connection.
         * @param handle Transport handle to disconnect.
         * @return transport_error describing the outcome.
         */
        transport_error transport_disconnect_impl(transport_handle *handle);

        /**
         * @brief Check whether the transport is currently connected.
         * @param handle Transport handle to query.
         * @return true when the transport is connected.
         */
        bool transport_is_connected_impl(const transport_handle *handle);
    } // namespace

    /**
     * @struct transport_handle
     * @brief Opaque transport state with function pointers for operations.
     */
    struct transport_handle {
        transport_type type;
        void *data; // Points to transport-specific data (udp_transport_data, etc.)
        // Function pointers for operations
        transport_error (*connect_fn)(transport_handle *, const transport_config &);
        transport_error (*send_fn)(transport_handle *, const protocol_message &);
        transport_receive_result (*receive_fn)(transport_handle *);
        transport_error (*disconnect_fn)(transport_handle *);
        bool (*is_connected_fn)(const transport_handle *);
        // Cleanup function
        void (*destroy_data_fn)(void *);
    };

    transport_error transport_connect(transport_handle *handle, const transport_config &config) {
        init_debug_category();
        info(transport, "transport_connect called for type: %d, host: %s, port: %u",
             handle ? static_cast<int>(handle->type) : -1, config.host.c_str(), config.port);

        if (!handle) {
            error(transport, "Transport handle is null");
            return transport_error::unknown_error;
        }

        if (!handle->connect_fn) {
            error(transport, "Transport handle missing connect function");
            return transport_error::unknown_error;
        }

        info(transport, "Calling transport-specific connect function...");
        const transport_error result = handle->connect_fn(handle, config);
        info(transport, "Transport connect function returned: %d", static_cast<int>(result));
        return result;
    }

    transport_error transport_send(transport_handle *handle, const protocol_message &message) {
        init_debug_category();

        if (!handle) {
            return transport_error::unknown_error;
        }

        if (!handle->send_fn) {
            error(transport, "Transport handle missing send function");
            return transport_error::unknown_error;
        }

        return handle->send_fn(handle, message);
    }

    transport_receive_result transport_receive(transport_handle *handle) {
        init_debug_category();

        if (!handle) {
            return transport_receive_result::failure(transport_error::unknown_error);
        }

        if (!handle->receive_fn) {
            error(transport, "Transport handle missing receive function");
            return transport_receive_result::failure(transport_error::unknown_error);
        }

        return handle->receive_fn(handle);
    }

    transport_error transport_disconnect(transport_handle *handle) {
        init_debug_category();

        if (!handle) {
            return transport_error::unknown_error;
        }

        if (!handle->disconnect_fn) {
            error(transport, "Transport handle missing disconnect function");
            return transport_error::unknown_error;
        }

        return handle->disconnect_fn(handle);
    }

    bool transport_is_connected(const transport_handle *handle) {
        if (!handle || !handle->is_connected_fn) {
            return false;
        }

        return handle->is_connected_fn(handle);
    }

    transport_type transport_get_type(const transport_handle *handle) {
        if (!handle) {
            return transport_type::udp; // Default
        }
        return handle->type;
    }

    void transport_set_webrtc_config(transport_handle *handle, const struct webrtc_config &webrtc_config_param) {
        if (!handle || handle->type != transport_type::webrtc) {
            return;
        }
        auto *webrtc_data = static_cast<webrtc_transport_data *>(handle->data);
        set_webrtc_config(webrtc_data, webrtc_config_param);
    }

    namespace {
        // UDP transport function wrappers that extract data from handle
        transport_error udp_connect_wrapper(transport_handle *handle, const transport_config &config) {
            auto *udp_data = static_cast<udp_transport_data *>(handle->data);
            return udp_connect_impl(udp_data, config);
        }

        transport_error udp_send_wrapper(transport_handle *handle, const protocol_message &message) {
            auto *udp_data = static_cast<udp_transport_data *>(handle->data);
            return udp_send_impl(udp_data, message);
        }

        transport_receive_result udp_receive_wrapper(transport_handle *handle) {
            auto *udp_data = static_cast<udp_transport_data *>(handle->data);
            return udp_receive_impl(udp_data);
        }

        transport_error udp_disconnect_wrapper(transport_handle *handle) {
            auto *udp_data = static_cast<udp_transport_data *>(handle->data);
            return udp_disconnect_impl(udp_data);
        }

        bool udp_is_connected_wrapper(const transport_handle *handle) {
            const auto *udp_data = static_cast<const udp_transport_data *>(handle->data);
            return udp_is_connected_impl(udp_data);
        }

        void udp_destroy_data_wrapper(void *data) {
            auto *udp_data = static_cast<udp_transport_data *>(data);
            destroy_udp_transport_data(udp_data);
        }

        // WebRTC transport function wrappers
        // Note: Pass empty webrtc_config - webrtc_connect_impl will use the one set in transport_data
        // via set_webrtc_config, or create a minimal one from transport_config
        transport_error webrtc_connect_wrapper(transport_handle *handle, const transport_config &config) {
            auto *webrtc_data = static_cast<webrtc_transport_data *>(handle->data);
            // Pass empty config - webrtc_connect_impl will use the one in transport_data if set,
            // or create one from transport_config
            webrtc_config empty_cfg{};
            return webrtc_connect_impl(webrtc_data, config, empty_cfg);
        }

        transport_error webrtc_send_wrapper(transport_handle *handle, const protocol_message &message) {
            auto *webrtc_data = static_cast<webrtc_transport_data *>(handle->data);
            return webrtc_send_impl(webrtc_data, message);
        }

        transport_receive_result webrtc_receive_wrapper(transport_handle *handle) {
            auto *webrtc_data = static_cast<webrtc_transport_data *>(handle->data);
            return webrtc_receive_impl(webrtc_data);
        }

        transport_error webrtc_disconnect_wrapper(transport_handle *handle) {
            auto *webrtc_data = static_cast<webrtc_transport_data *>(handle->data);
            return webrtc_disconnect_impl(webrtc_data);
        }

        bool webrtc_is_connected_wrapper(const transport_handle *handle) {
            const auto *webrtc_data = static_cast<const webrtc_transport_data *>(handle->data);
            return webrtc_is_connected_impl(webrtc_data);
        }

        void webrtc_destroy_data_wrapper(void *data) {
            auto *webrtc_data = static_cast<webrtc_transport_data *>(data);
            destroy_webrtc_transport_data(webrtc_data);
        }
    } // namespace

    std::unique_ptr<transport_handle, void (*)(transport_handle *)> create_transport(transport_type type) {
        init_debug_category();

        auto handle = std::unique_ptr<transport_handle, void (*)(transport_handle *)>(new transport_handle{},
                                                                                      destroy_transport);

        handle->type = type;

        switch (type) {
            case transport_type::udp: {
                auto udp_data = create_udp_transport_data();
                if (!udp_data) {
                    error(transport, "Failed to create UDP transport data");
                    return std::unique_ptr<transport_handle, void (*)(transport_handle *)>(nullptr, destroy_transport);
                }
                handle->data = udp_data.release();

                // Set function pointers for UDP operations
                handle->connect_fn = udp_connect_wrapper;
                handle->send_fn = udp_send_wrapper;
                handle->receive_fn = udp_receive_wrapper;
                handle->disconnect_fn = udp_disconnect_wrapper;
                handle->is_connected_fn = udp_is_connected_wrapper;
                handle->destroy_data_fn = udp_destroy_data_wrapper;

                info(transport, "Created UDP transport");
                break;
            }
            case transport_type::tcp:
                error(transport, "TCP transport not yet implemented");
                return std::unique_ptr<transport_handle, void (*)(transport_handle *)>(nullptr, destroy_transport);
            case transport_type::websocket:
                error(transport, "WebSocket transport not yet implemented");
                return std::unique_ptr<transport_handle, void (*)(transport_handle *)>(nullptr, destroy_transport);
            case transport_type::webrtc: {
                auto webrtc_data = create_webrtc_transport_data();
                if (!webrtc_data) {
                    error(transport, "Failed to create WebRTC transport data");
                    return std::unique_ptr<transport_handle, void (*)(transport_handle *)>(nullptr, destroy_transport);
                }
                handle->data = webrtc_data.release();

                // Set function pointers for WebRTC operations
                // WebRTC connect wrapper will use webrtc_config from transport_data if set
                // (set via manager_set_webrtc_config -> set_webrtc_config)
                handle->connect_fn = webrtc_connect_wrapper;
                handle->send_fn = webrtc_send_wrapper;
                handle->receive_fn = webrtc_receive_wrapper;
                handle->disconnect_fn = webrtc_disconnect_wrapper;
                handle->is_connected_fn = webrtc_is_connected_wrapper;
                handle->destroy_data_fn = webrtc_destroy_data_wrapper;

                info(transport, "Created WebRTC transport");
                break;
            }
        }

        return handle;
    }

    void destroy_transport(transport_handle *handle) {
        if (!handle) {
            return;
        }
        std::unique_ptr<transport_handle> owned_handle(handle);

        init_debug_category();

        // Disconnect if still connected
        if (owned_handle->is_connected_fn && owned_handle->is_connected_fn(owned_handle.get())) {
            if (owned_handle->disconnect_fn) {
                owned_handle->disconnect_fn(owned_handle.get());
            }
        }

        // Destroy transport-specific data
        if (owned_handle->destroy_data_fn && owned_handle->data) {
            owned_handle->destroy_data_fn(owned_handle->data);
            owned_handle->data = nullptr;
        }
    }
} // namespace sf::communication
