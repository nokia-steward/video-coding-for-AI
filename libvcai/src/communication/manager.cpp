// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/communication/manager.h"
#include "../../include/communication/transport_webrtc.h"
#include "manager_test_hooks.h"

#include <gst/gst.h>
#include <optional>
#include "../../include/logging.h"

// Need access to transport_handle internals - include the implementation
// Note: This is a workaround - ideally transport_handle would be in a header
// For now, we'll access it through the transport functions

namespace sf::communication {
    namespace {
        struct manager_transport_api {
            manager_create_transport_fn create_transport_fn = create_transport;
            manager_transport_get_type_fn transport_get_type_fn = transport_get_type;
            manager_transport_set_webrtc_config_fn transport_set_webrtc_config_fn = transport_set_webrtc_config;
            manager_transport_connect_fn transport_connect_fn = transport_connect;
            manager_transport_send_fn transport_send_fn = transport_send;
            manager_transport_receive_fn transport_receive_fn = transport_receive;
            manager_transport_disconnect_fn transport_disconnect_fn = transport_disconnect;
        };

        [[nodiscard]] manager_transport_api &get_manager_transport_api() {
            static manager_transport_api api{};
            return api;
        }

        // Debug category for communication manager operations.
        GST_DEBUG_CATEGORY_STATIC(comm_manager);
#define GST_CAT_DEFAULT comm_manager

        /**
         * @brief Initialize the communication manager debug category once.
         */
        void init_debug_category() {
            static bool initialized = false;
            if (!initialized) {
                GST_DEBUG_CATEGORY_INIT(comm_manager, "communication-manager", 0, "Communication manager");
                initialized = true;
            }
        }
    } // namespace

    /**
     * @struct communication_manager
     * @brief Owns transport state and session metadata for signaling.
     */
    struct communication_manager {
        std::unique_ptr<transport_handle, void (*)(transport_handle *)> transport;
        transport_config config;
        std::optional<webrtc_config> webrtc_config_data;
        bool initialized;
        uint32_t session_id;

        /**
         * @brief Construct a communication manager around a transport handle.
         * @param transport_param Transport handle ownership.
         */
        communication_manager(std::unique_ptr<transport_handle, void (*)(transport_handle *)> transport_param) :
            transport(std::move(transport_param)), config{}, webrtc_config_data(std::nullopt), initialized(false),
            session_id(0) {}
    };

    std::unique_ptr<communication_manager, void (*)(communication_manager *)>
    create_communication_manager(const transport_type type) {
        init_debug_category();

        auto transport_handle = get_manager_transport_api().create_transport_fn(type);
        if (!transport_handle) {
            error(comm_manager, "Failed to create transport for communication manager");
            return std::unique_ptr<communication_manager, void (*)(communication_manager *)>(
                    nullptr, destroy_communication_manager);
        }

        auto manager = std::unique_ptr<communication_manager, void (*)(communication_manager *)>(
                new communication_manager(std::move(transport_handle)), destroy_communication_manager);

        info(comm_manager, "Created communication manager");
        return manager;
    }

    void manager_set_webrtc_config(communication_manager *manager, const webrtc_config &webrtc_config_param) {
        if (!manager) {
            return;
        }

        init_debug_category();

        manager->webrtc_config_data = webrtc_config_param;

        // Also set it in the transport data if transport is WebRTC
        if (get_manager_transport_api().transport_get_type_fn(manager->transport.get()) == transport_type::webrtc) {
            get_manager_transport_api().transport_set_webrtc_config_fn(manager->transport.get(), webrtc_config_param);
        }

        info(comm_manager, "WebRTC configuration set");
    }

    void destroy_communication_manager(communication_manager *manager) {
        if (!manager) {
            return;
        }
        std::unique_ptr<communication_manager> owned_manager(manager);

        init_debug_category();

        // Shutdown if initialized
        if (owned_manager->initialized) {
            [[maybe_unused]] const transport_error shutdown_error = manager_shutdown(owned_manager.get());
        }
    }

    transport_error manager_initialize(communication_manager *manager, const transport_config &config) {
        init_debug_category();
        info(comm_manager, "Initializing communication manager for %s:%u", config.host.c_str(), config.port);

        if (!manager) {
            error(comm_manager, "Manager is null");
            return transport_error::unknown_error;
        }

        if (manager->initialized) {
            warning(comm_manager, "Communication manager already initialized");
            return transport_error::success;
        }

        manager->config = config;

        // Check transport type
        const transport_type type = get_manager_transport_api().transport_get_type_fn(manager->transport.get());
        info(comm_manager, "Transport type: %d", static_cast<int>(type));

        // Connect transport
        // The webrtc_connect_wrapper will use webrtc_config from transport_data if set
        // (set via manager_set_webrtc_config -> set_webrtc_config)
        info(comm_manager, "Connecting transport...");
        const transport_error transport_error =
                get_manager_transport_api().transport_connect_fn(manager->transport.get(), config);

        if (transport_error != transport_error::success) {
            error(comm_manager, "Failed to connect transport during initialization: %d",
                  static_cast<int>(transport_error));
            return transport_error;
        }

        manager->initialized = true;
        info(comm_manager, "Communication manager initialized successfully for %s:%u", config.host.c_str(),
             config.port);

        return transport_error::success;
    }

    namespace {
        /**
         * @brief Sends a handshake request.
         * @param manager Communication manager.
         * @return Error code, or success if sent successfully.
         */
        [[nodiscard]] transport_error send_handshake_request(communication_manager *manager) {
            info(comm_manager, "Creating handshake request...");
            constexpr handshake_request request{};
            const protocol_message request_msg = serialize_handshake_request(request);
            info(comm_manager, "Handshake request message created, type: %d", static_cast<int>(request_msg.type));

            info(comm_manager, "Sending handshake request...");
            const transport_error send_error =
                    get_manager_transport_api().transport_send_fn(manager->transport.get(), request_msg);
            if (send_error != transport_error::success) {
                error(comm_manager, "Failed to send handshake request: %d", static_cast<int>(send_error));
                return send_error;
            }

            info(comm_manager, "Handshake request sent successfully");
            return transport_error::success;
        }

        /**
         * @brief Receives and deserializes a handshake response.
         * @param manager Communication manager.
         * @return Handshake response or empty optional on failure.
         */
        [[nodiscard]] std::optional<handshake_response> receive_handshake_response(communication_manager *manager) {
            info(comm_manager, "Waiting for handshake response...");
            const auto receive_result = get_manager_transport_api().transport_receive_fn(manager->transport.get());
            if (receive_result.error != transport_error::success || !receive_result.message) {
                error(comm_manager, "Failed to receive handshake response: error=%d, has_message=%d",
                      static_cast<int>(receive_result.error), receive_result.message.has_value() ? 1 : 0);
                return std::nullopt;
            }
            info(comm_manager, "Handshake response received successfully");

            const protocol_message &response_msg = receive_result.message.value();

            // Deserialize handshake response
            const auto response_opt = deserialize_handshake_response(response_msg);
            if (!response_opt) {
                error(comm_manager, "Failed to deserialize handshake response");
                return std::nullopt;
            }

            return response_opt;
        }
    } // namespace

    handshake_result_wrapper manager_perform_handshake(communication_manager *manager) {
        init_debug_category();
        info(comm_manager, "manager_perform_handshake called");

        if (!manager) {
            error(comm_manager, "Manager is null");
            return handshake_result_wrapper::failure(transport_error::unknown_error);
        }

        if (!manager->initialized) {
            error(comm_manager, "Communication manager not initialized");
            return handshake_result_wrapper::failure(transport_error::not_connected);
        }

        // Send handshake request
        const transport_error send_error = send_handshake_request(manager);
        if (send_error != transport_error::success) {
            return handshake_result_wrapper::failure(send_error);
        }

        info(comm_manager, "Waiting for handshake response...");

        // Receive and deserialize handshake response
        const auto response_opt = receive_handshake_response(manager);
        if (!response_opt) {
            return handshake_result_wrapper::failure(transport_error::invalid_message);
        }

        const handshake_response &response = response_opt.value();

        // Store session ID
        manager->session_id = response.session_id;

        info(comm_manager, "Handshake successful: port=%u, session_id=%u", response.assigned_port, response.session_id);

        return handshake_result_wrapper::success(handshake_result{response.assigned_port, response.session_id});
    }

    transport_error manager_send_heartbeat(communication_manager *manager, const uint16_t streaming_port,
                                           const uint32_t session_id) {
        init_debug_category();

        if (!manager) {
            return transport_error::unknown_error;
        }

        if (!manager->initialized) {
            error(comm_manager, "Communication manager not initialized");
            return transport_error::not_connected;
        }

        // Create and send heartbeat message
        const heartbeat_message heartbeat{streaming_port, session_id};
        const protocol_message heartbeat_msg = serialize_heartbeat(heartbeat);

        const transport_error error =
                get_manager_transport_api().transport_send_fn(manager->transport.get(), heartbeat_msg);
        if (error != transport_error::success) {
            warning(comm_manager, "Failed to send heartbeat: port=%u, session_id=%u", streaming_port, session_id);
            return error;
        }

        debug(comm_manager, "Heartbeat sent: port=%u session_id=%u", streaming_port, session_id);
        return transport_error::success;
    }

    transport_error manager_send_disconnect(communication_manager *manager, const uint16_t streaming_port,
                                            const uint32_t session_id) {
        init_debug_category();

        if (!manager) {
            return transport_error::unknown_error;
        }

        if (!manager->initialized) {
            error(comm_manager, "Communication manager not initialized");
            return transport_error::not_connected;
        }

        const disconnect_message disconnect{streaming_port, session_id};
        const protocol_message disconnect_msg = serialize_disconnect_request(disconnect);

        const transport_error error =
                get_manager_transport_api().transport_send_fn(manager->transport.get(), disconnect_msg);
        if (error != transport_error::success) {
            warning(comm_manager, "Failed to send disconnect request: port=%u, session_id=%u", streaming_port,
                    session_id);
            return error;
        }

        info(comm_manager, "Disconnect request sent: port=%u session_id=%u", streaming_port, session_id);
        return transport_error::success;
    }

    transport_error manager_wait_for_disconnect_ack(communication_manager *manager, const uint16_t streaming_port,
                                                    const uint32_t session_id) {
        init_debug_category();

        if (!manager) {
            return transport_error::unknown_error;
        }

        if (!manager->initialized) {
            error(comm_manager, "Communication manager not initialized");
            return transport_error::not_connected;
        }

        const auto receive_result = get_manager_transport_api().transport_receive_fn(manager->transport.get());
        if (receive_result.error != transport_error::success || !receive_result.message) {
            warning(comm_manager, "Failed to receive disconnect ack: error=%d, has_message=%d",
                    static_cast<int>(receive_result.error), receive_result.message.has_value() ? 1 : 0);
            return receive_result.error != transport_error::success ? receive_result.error
                                                                    : transport_error::invalid_message;
        }

        const protocol_message &response_msg = receive_result.message.value();
        const auto ack_opt = deserialize_disconnect_ack(response_msg);
        if (!ack_opt) {
            warning(comm_manager, "Failed to deserialize disconnect ack");
            return transport_error::invalid_message;
        }

        if (ack_opt->streaming_port != streaming_port || ack_opt->session_id != session_id) {
            warning(comm_manager,
                    "Disconnect ack does not match request (expected port=%u session=%u, got port=%u session=%u)",
                    streaming_port, session_id, ack_opt->streaming_port, ack_opt->session_id);
            return transport_error::invalid_message;
        }

        info(comm_manager, "Disconnect ack received: port=%u session_id=%u", streaming_port, session_id);
        return transport_error::success;
    }

    transport_error manager_wait_for_stream_ready(communication_manager *manager, const uint16_t streaming_port,
                                                  const uint32_t session_id) {
        init_debug_category();

        if (!manager) {
            return transport_error::unknown_error;
        }

        if (!manager->initialized) {
            error(comm_manager, "Communication manager not initialized");
            return transport_error::not_connected;
        }

        const auto receive_result = get_manager_transport_api().transport_receive_fn(manager->transport.get());
        if (receive_result.error != transport_error::success || !receive_result.message) {
            warning(comm_manager, "Failed to receive stream-ready message: error=%d, has_message=%d",
                    static_cast<int>(receive_result.error), receive_result.message.has_value() ? 1 : 0);
            return receive_result.error != transport_error::success ? receive_result.error
                                                                    : transport_error::invalid_message;
        }

        const protocol_message &response_msg = receive_result.message.value();
        const auto ready_opt = deserialize_stream_ready(response_msg);
        if (!ready_opt) {
            warning(comm_manager, "Failed to deserialize stream-ready message");
            return transport_error::invalid_message;
        }

        if (ready_opt->streaming_port != streaming_port || ready_opt->session_id != session_id) {
            warning(comm_manager,
                    "Stream-ready message does not match handshake (expected port=%u session=%u, got port=%u "
                    "session=%u)",
                    streaming_port, session_id, ready_opt->streaming_port, ready_opt->session_id);
            return transport_error::invalid_message;
        }

        info(comm_manager, "Stream-ready received: port=%u session_id=%u", streaming_port, session_id);
        return transport_error::success;
    }

    transport_error manager_shutdown(communication_manager *manager) {
        init_debug_category();

        if (!manager) {
            return transport_error::unknown_error;
        }

        if (!manager->initialized) {
            return transport_error::success;
        }

        const transport_error error = get_manager_transport_api().transport_disconnect_fn(manager->transport.get());
        if (error != transport_error::success) {
            warning(comm_manager, "Error during transport disconnect: %d", static_cast<int>(error));
        }

        manager->initialized = false;
        manager->session_id = 0;

        info(comm_manager, "Communication manager shut down");

        return transport_error::success;
    }

    void manager_test_set_transport_hooks(const manager_create_transport_fn create_transport_fn,
                                          const manager_transport_get_type_fn transport_get_type_fn,
                                          const manager_transport_set_webrtc_config_fn transport_set_webrtc_config_fn,
                                          const manager_transport_connect_fn transport_connect_fn,
                                          const manager_transport_send_fn transport_send_fn,
                                          const manager_transport_receive_fn transport_receive_fn,
                                          const manager_transport_disconnect_fn transport_disconnect_fn) {
        auto &api = get_manager_transport_api();
        api.create_transport_fn = create_transport_fn ? create_transport_fn : create_transport;
        api.transport_get_type_fn = transport_get_type_fn ? transport_get_type_fn : transport_get_type;
        api.transport_set_webrtc_config_fn =
                transport_set_webrtc_config_fn ? transport_set_webrtc_config_fn : transport_set_webrtc_config;
        api.transport_connect_fn = transport_connect_fn ? transport_connect_fn : transport_connect;
        api.transport_send_fn = transport_send_fn ? transport_send_fn : transport_send;
        api.transport_receive_fn = transport_receive_fn ? transport_receive_fn : transport_receive;
        api.transport_disconnect_fn = transport_disconnect_fn ? transport_disconnect_fn : transport_disconnect;
    }

    void manager_test_reset_transport_hooks() {
        auto &api = get_manager_transport_api();
        api = manager_transport_api{};
    }

} // namespace sf::communication
