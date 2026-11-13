// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "protocol.h"
#include "transport.h"
#include "transport_webrtc.h"

#include <memory>
#include <optional>

namespace sf {
    namespace communication {

        /**
         * @file manager.h
         * @brief High-level communication manager for handshake and heartbeat operations.
         *
         * This module provides a function-based API for communication operations.
         * It coordinates protocol and transport layers without using inheritance.
         */

        /**
         * @struct handshake_result
         * @brief Result of a successful handshake operation.
         */
        struct handshake_result {
            uint16_t assigned_port; ///< Port assigned for video streaming
            uint32_t session_id; ///< Session identifier
        };

        /**
         * @struct communication_manager
         * @brief Opaque handle for communication manager.
         */
        struct communication_manager;

        /**
         * @brief Creates a communication manager with the specified transport type.
         * @param type The transport type to use.
         * @return Unique pointer to communication manager with custom deleter, or nullptr on failure.
         */
        [[nodiscard]] std::unique_ptr<communication_manager, void (*)(communication_manager *)>
        create_communication_manager(transport_type type);

        /**
         * @brief Destroys a communication manager and cleans up resources.
         * @param manager Communication manager to destroy (can be nullptr).
         */
        void destroy_communication_manager(communication_manager *manager);

        /**
         * @brief Sets WebRTC configuration (only needed for WebRTC transport).
         * @param manager Communication manager (must be valid).
         * @param webrtc_config WebRTC-specific configuration.
         */
        void manager_set_webrtc_config(communication_manager *manager, const webrtc_config &webrtc_config);

        /**
         * @brief Initializes the communication manager with connection configuration.
         * @param manager Communication manager (must be valid).
         * @param config Connection configuration.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error manager_initialize(communication_manager *manager,
                                                         const transport_config &config);

        /**
         * @struct handshake_result_wrapper
         * @brief Wrapper for handshake result with error information.
         */
        struct handshake_result_wrapper {
            transport_error error; ///< Error code (success if result is valid)
            std::optional<handshake_result> result; ///< Handshake result (valid if error is success)

            [[nodiscard]] static handshake_result_wrapper success(handshake_result res) {
                return {transport_error::success, res};
            }

            [[nodiscard]] static handshake_result_wrapper failure(transport_error err) { return {err, std::nullopt}; }
        };

        /**
         * @brief Performs a handshake with the server.
         * @param manager Communication manager (must be valid and initialized).
         * @return Wrapper containing handshake result or error code.
         */
        [[nodiscard]] handshake_result_wrapper manager_perform_handshake(communication_manager *manager);

        /**
         * @brief Sends a heartbeat message to the server.
         * @param manager Communication manager (must be valid and initialized).
         * @param streaming_port The port being used for video streaming.
         * @param session_id The session identifier.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error manager_send_heartbeat(communication_manager *manager,
                                                             const uint16_t streaming_port, const uint32_t session_id);

        /**
         * @brief Sends a disconnect request message to the server.
         * @param manager Communication manager (must be valid and initialized).
         * @param streaming_port The port being used for video streaming.
         * @param session_id The session identifier.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error manager_send_disconnect(communication_manager *manager,
                                                              const uint16_t streaming_port, const uint32_t session_id);

        /**
         * @brief Waits for a disconnect ack from the server.
         * @param manager Communication manager (must be valid and initialized).
         * @param streaming_port The port being used for video streaming.
         * @param session_id The session identifier.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error manager_wait_for_disconnect_ack(communication_manager *manager,
                                                                      const uint16_t streaming_port,
                                                                      const uint32_t session_id);

        /**
         * @brief Waits for a stream-ready notification from the server.
         * @param manager Communication manager (must be valid and initialized).
         * @param streaming_port The port assigned by handshake.
         * @param session_id The session identifier assigned by handshake.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error manager_wait_for_stream_ready(communication_manager *manager,
                                                                    uint16_t streaming_port, uint32_t session_id);

        /**
         * @brief Shuts down the communication manager and disconnects.
         * @param manager Communication manager (must be valid).
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error manager_shutdown(communication_manager *manager);

    } // namespace communication
} // namespace sf
