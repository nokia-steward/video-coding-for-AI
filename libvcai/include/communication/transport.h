// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "protocol.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace sf {
    namespace communication {

        /**
         * @file transport.h
         * @brief Transport layer interface for communication.
         *
         * This module provides a function-based transport abstraction without inheritance.
         * It follows functional programming principles:
         * - Function-based interfaces (no virtual functions)
         * - Opaque handles for implementation details
         * - Type dispatch via enums
         * - RAII resource management
         */

        /**
         * @enum transport_type
         * @brief Defines available transport types.
         */
        enum class transport_type { udp, tcp, websocket, webrtc };

        /**
         * @enum transport_error
         * @brief Error codes for transport operations.
         */
        enum class transport_error {
            success,
            connection_failed,
            timeout,
            invalid_message,
            send_failed,
            receive_failed,
            not_connected,
            unknown_error
        };

        /**
         * @struct transport_config
         * @brief Configuration for transport connection.
         */
        struct transport_config {
            std::string host; ///< Server host address
            uint16_t port; ///< Server port
            std::chrono::milliseconds timeout; ///< Operation timeout
        };

        /**
         * @struct transport_handle
         * @brief Opaque handle for transport implementation.
         *
         * This struct contains function pointers for transport operations and
         * implementation-specific data. It's created by transport factory functions.
         */
        struct transport_handle;

        /**
         * @brief Connects the transport to the configured server.
         * @param handle Transport handle (must be valid).
         * @param config Connection configuration.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error transport_connect(transport_handle *handle, const transport_config &config);

        /**
         * @brief Sends a protocol message through the transport.
         * @param handle Transport handle (must be valid and connected).
         * @param message The message to send.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error transport_send(transport_handle *handle, const protocol_message &message);

        /**
         * @struct transport_receive_result
         * @brief Result of a receive operation.
         */
        struct transport_receive_result {
            transport_error error; ///< Error code (success if message is valid)
            std::optional<protocol_message> message; ///< Received message (valid if error is success)

            [[nodiscard]] static transport_receive_result success(protocol_message msg) {
                return {transport_error::success, std::move(msg)};
            }

            [[nodiscard]] static transport_receive_result failure(transport_error err) { return {err, std::nullopt}; }
        };

        /**
         * @brief Receives a protocol message from the transport.
         * @param handle Transport handle (must be valid and connected).
         * @return Result containing the message or error code.
         */
        [[nodiscard]] transport_receive_result transport_receive(transport_handle *handle);

        /**
         * @brief Disconnects the transport.
         * @param handle Transport handle (must be valid).
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error transport_disconnect(transport_handle *handle);

        /**
         * @brief Checks if the transport is connected.
         * @param handle Transport handle (must be valid).
         * @return true if connected, false otherwise.
         */
        [[nodiscard]] bool transport_is_connected(const transport_handle *handle);

        /**
         * @brief Gets the transport type from a transport handle.
         * @param handle Transport handle (must be valid).
         * @return The transport type.
         */
        [[nodiscard]] transport_type transport_get_type(const transport_handle *handle);

        /**
         * @brief Sets WebRTC configuration in a WebRTC transport handle.
         * @param handle Transport handle (must be valid and of type webrtc).
         * @param webrtc_config_param WebRTC configuration to set.
         */
        void transport_set_webrtc_config(transport_handle *handle, const struct webrtc_config &webrtc_config_param);

        /**
         * @brief Creates a transport handle for the specified transport type.
         * @param type The transport type to create.
         * @return Unique pointer to transport handle with custom deleter, or nullptr on failure.
         */
        [[nodiscard]] std::unique_ptr<transport_handle, void (*)(transport_handle *)>
        create_transport(transport_type type);

        /**
         * @brief Destroys a transport handle and cleans up resources.
         * @param handle Transport handle to destroy (can be nullptr).
         */
        void destroy_transport(transport_handle *handle);

    } // namespace communication
} // namespace sf
