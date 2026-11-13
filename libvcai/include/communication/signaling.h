// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace sf {
    namespace communication {

        /**
         * @file signaling.h
         * @brief WebSocket-based signaling for WebRTC.
         *
         * This module provides WebSocket signaling client and server functionality
         * for WebRTC SDP offer/answer and ICE candidate exchange.
         */

        /**
         * @enum signaling_message_type
         * @brief Types of signaling messages.
         */
        enum class signaling_message_type {
            sdp_offer, ///< SDP offer
            sdp_answer, ///< SDP answer
            ice_candidate, ///< ICE candidate
            handshake, ///< Handshake (reuses protocol)
            heartbeat, ///< Heartbeat (reuses protocol)
            disconnect ///< Disconnect (reuses protocol)
        };

        /**
         * @struct signaling_message
         * @brief Signaling message structure.
         */
        struct signaling_message {
            signaling_message_type type;
            std::string data; ///< Message payload (SDP, ICE candidate, etc.)

            signaling_message() : type(signaling_message_type::sdp_offer), data() {}
            signaling_message(signaling_message_type type_param, std::string data_param) :
                type(type_param), data(std::move(data_param)) {}
        };

        /**
         * @struct signaling_client
         * @brief Opaque handle for signaling client.
         */
        struct signaling_client;

        /**
         * @brief Callback type for received signaling messages.
         * @param message The received signaling message.
         */
        using signaling_message_callback = std::function<void(const signaling_message &message)>;

        /**
         * @brief Creates a signaling client.
         * @param url WebSocket URL (e.g., ws://127.0.0.1:8080).
         * @param callback Callback for received messages.
         * @return Unique pointer to signaling client, or nullptr on failure.
         */
        [[nodiscard]] std::unique_ptr<signaling_client, void (*)(signaling_client *)>
        create_signaling_client(const std::string &url, signaling_message_callback callback);

        /**
         * @brief Destroys a signaling client.
         * @param client Signaling client to destroy (can be nullptr).
         */
        void destroy_signaling_client(signaling_client *client);

        /**
         * @brief Connects the signaling client to the server.
         * @param client Signaling client (must be valid).
         * @return true on success, false otherwise.
         */
        [[nodiscard]] bool signaling_client_connect(signaling_client *client);

        /**
         * @brief Sends a signaling message.
         * @param client Signaling client (must be valid and connected).
         * @param message The message to send.
         * @return true on success, false otherwise.
         */
        [[nodiscard]] bool signaling_client_send(signaling_client *client, const signaling_message &message);

        /**
         * @brief Disconnects the signaling client.
         * @param client Signaling client (must be valid).
         */
        void signaling_client_disconnect(signaling_client *client);

        /**
         * @brief Checks if the signaling client is connected.
         * @param client Signaling client (must be valid).
         * @return true if connected, false otherwise.
         */
        [[nodiscard]] bool signaling_client_is_connected(const signaling_client *client);

        /**
         * @struct signaling_server
         * @brief Opaque handle for signaling server.
         */
        struct signaling_server;

        /**
         * @brief Callback type for new client connections.
         * @param client_id Unique client identifier.
         */
        using signaling_client_connected_callback = std::function<void(const std::string &client_id)>;

        /**
         * @brief Callback type for client disconnections.
         * @param client_id Unique client identifier.
         */
        using signaling_client_disconnected_callback = std::function<void(const std::string &client_id)>;

        /**
         * @brief Callback type for received messages from clients.
         * @param client_id Client identifier.
         * @param message The received signaling message.
         */
        using signaling_server_message_callback =
                std::function<void(const std::string &client_id, const signaling_message &message)>;

        /**
         * @brief Creates a signaling server.
         * @param port Port to listen on.
         * @param use_ssl Whether to use WSS (WebSocket Secure).
         * @param client_connected_callback Callback for new client connections.
         * @param client_disconnected_callback Callback for client disconnections.
         * @param message_callback Callback for received messages.
         * @return Unique pointer to signaling server, or nullptr on failure.
         */
        [[nodiscard]] std::unique_ptr<signaling_server, void (*)(signaling_server *)>
        create_signaling_server(uint16_t port, bool use_ssl,
                                signaling_client_connected_callback client_connected_callback,
                                signaling_client_disconnected_callback client_disconnected_callback,
                                signaling_server_message_callback message_callback);

        /**
         * @brief Destroys a signaling server.
         * @param server Signaling server to destroy (can be nullptr).
         */
        void destroy_signaling_server(signaling_server *server);

        /**
         * @brief Starts the signaling server.
         * @param server Signaling server (must be valid).
         * @return true on success, false otherwise.
         */
        [[nodiscard]] bool signaling_server_start(signaling_server *server);

        /**
         * @brief Stops the signaling server.
         * @param server Signaling server (must be valid).
         */
        void signaling_server_stop(signaling_server *server);

        /**
         * @brief Sends a message to a specific client.
         * @param server Signaling server (must be valid and started).
         * @param client_id Client identifier.
         * @param message The message to send.
         * @return true on success, false otherwise.
         */
        [[nodiscard]] bool signaling_server_send_to_client(signaling_server *server, const std::string &client_id,
                                                           const signaling_message &message);

    } // namespace communication
} // namespace sf
