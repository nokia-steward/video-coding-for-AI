// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace sf {
    namespace communication {

        /**
         * @file protocol.h
         * @brief Protocol message definitions and serialization for communication layer.
         *
         * This module provides a binary protocol for communication between encoder_cli and rtp_server.
         * It follows functional programming principles:
         * - Pure serialization/deserialization functions
         * - Immutable message structures
         * - Error handling through return values
         * - No global state
         */

        /**
         * @enum protocol_version
         * @brief Defines protocol versions.
         */
        enum class protocol_version : uint8_t { v1 = 1 };

        /**
         * @enum message_type
         * @brief Defines the types of protocol messages.
         */
        enum class message_type : uint8_t {
            handshake_request = 0x01,
            handshake_response = 0x02,
            heartbeat = 0x03,
            disconnect_request = 0x04,
            disconnect_ack = 0x05,
            stream_ready = 0x06,
            error = 0xFF
        };

        /**
         * @struct protocol_message
         * @brief Represents a protocol message with version, type, and payload.
         *
         * This struct is immutable once created. Serialization creates a new byte vector.
         */
        struct protocol_message {
            protocol_version version; ///< Protocol version
            message_type type; ///< Message type
            std::vector<uint8_t> payload; ///< Message-specific payload data

            /**
             * @brief Default constructor creates an invalid message.
             */
            protocol_message() : version(protocol_version::v1), type(message_type::error), payload() {}

            /**
             * @brief Constructs a protocol message.
             * @param version_param Protocol version.
             * @param type_param Message type.
             * @param payload_param Payload data (moved).
             */
            protocol_message(const protocol_version version_param, const message_type type_param,
                             std::vector<uint8_t> payload_param) :
                version(version_param),
                type(type_param), payload(std::move(payload_param)) {}
        };

        /**
         * @struct handshake_request
         * @brief Handshake request message (client -> server).
         *
         * Currently empty, but can be extended with client capabilities, authentication, etc.
         */
        struct handshake_request {
            // Empty for now, can be extended
        };

        /**
         * @struct handshake_response
         * @brief Handshake response message (server -> client).
         */
        struct handshake_response {
            uint16_t assigned_port; ///< Port assigned for video streaming
            uint32_t session_id; ///< Session identifier for this connection
        };

        /**
         * @struct heartbeat_message
         * @brief Heartbeat message (client -> server).
         */
        struct heartbeat_message {
            uint16_t streaming_port; ///< Port being used for streaming
            uint32_t session_id; ///< Session identifier
        };

        /**
         * @struct disconnect_message
         * @brief Disconnect message (client -> server, server -> client ack).
         */
        struct disconnect_message {
            uint16_t streaming_port; ///< Port being used for streaming
            uint32_t session_id; ///< Session identifier
        };

        /**
         * @struct stream_ready_message
         * @brief Stream-ready message (server -> client).
         */
        struct stream_ready_message {
            uint16_t streaming_port; ///< Port assigned for streaming
            uint32_t session_id; ///< Session identifier
        };

        /**
         * @brief Serializes a protocol message to a byte vector.
         * @param message The message to serialize.
         * @return Serialized byte vector.
         *
         * Format: [version:1][type:1][payload_length:2][payload:N]
         */
        [[nodiscard]] std::vector<uint8_t> serialize_message(const protocol_message &message);

        /**
         * @brief Deserializes a byte vector to a protocol message.
         * @param data The byte vector to deserialize.
         * @return The deserialized message, or empty message if deserialization fails.
         */
        [[nodiscard]] std::optional<protocol_message> deserialize_message(const std::vector<uint8_t> &data);

        /**
         * @brief Serializes a handshake request to a protocol message.
         * @param request The handshake request.
         * @param version Protocol version (default: v1).
         * @return Protocol message containing the serialized request.
         */
        [[nodiscard]] protocol_message serialize_handshake_request(const handshake_request &request,
                                                                   protocol_version version = protocol_version::v1);

        /**
         * @brief Deserializes a protocol message to a handshake request.
         * @param message The protocol message.
         * @return The handshake request, or std::nullopt if deserialization fails.
         */
        [[nodiscard]] std::optional<handshake_request> deserialize_handshake_request(const protocol_message &message);

        /**
         * @brief Serializes a handshake response to a protocol message.
         * @param response The handshake response.
         * @param version Protocol version (default: v1).
         * @return Protocol message containing the serialized response.
         */
        [[nodiscard]] protocol_message serialize_handshake_response(const handshake_response &response,
                                                                    protocol_version version = protocol_version::v1);

        /**
         * @brief Deserializes a protocol message to a handshake response.
         * @param message The protocol message.
         * @return The handshake response, or std::nullopt if deserialization fails.
         */
        [[nodiscard]] std::optional<handshake_response> deserialize_handshake_response(const protocol_message &message);

        /**
         * @brief Serializes a heartbeat message to a protocol message.
         * @param heartbeat The heartbeat message.
         * @param version Protocol version (default: v1).
         * @return Protocol message containing the serialized heartbeat.
         */
        [[nodiscard]] protocol_message serialize_heartbeat(const heartbeat_message &heartbeat,
                                                           protocol_version version = protocol_version::v1);

        /**
         * @brief Deserializes a protocol message to a heartbeat message.
         * @param message The protocol message.
         * @return The heartbeat message, or std::nullopt if deserialization fails.
         */
        [[nodiscard]] std::optional<heartbeat_message> deserialize_heartbeat(const protocol_message &message);

        /**
         * @brief Serializes a disconnect request to a protocol message.
         * @param disconnect The disconnect message.
         * @param version Protocol version (default: v1).
         * @return Protocol message containing the serialized disconnect request.
         */
        [[nodiscard]] protocol_message serialize_disconnect_request(const disconnect_message &disconnect,
                                                                    protocol_version version = protocol_version::v1);

        /**
         * @brief Deserializes a protocol message to a disconnect request.
         * @param message The protocol message.
         * @return The disconnect message, or std::nullopt if deserialization fails.
         */
        [[nodiscard]] std::optional<disconnect_message> deserialize_disconnect_request(const protocol_message &message);

        /**
         * @brief Serializes a disconnect ack to a protocol message.
         * @param disconnect The disconnect message.
         * @param version Protocol version (default: v1).
         * @return Protocol message containing the serialized disconnect ack.
         */
        [[nodiscard]] protocol_message serialize_disconnect_ack(const disconnect_message &disconnect,
                                                                protocol_version version = protocol_version::v1);

        /**
         * @brief Deserializes a protocol message to a disconnect ack.
         * @param message The protocol message.
         * @return The disconnect message, or std::nullopt if deserialization fails.
         */
        [[nodiscard]] std::optional<disconnect_message> deserialize_disconnect_ack(const protocol_message &message);

        /**
         * @brief Serializes a stream-ready message to a protocol message.
         * @param ready Stream-ready message.
         * @param version Protocol version (default: v1).
         * @return Protocol message containing stream-ready payload.
         */
        [[nodiscard]] protocol_message serialize_stream_ready(const stream_ready_message &ready,
                                                              protocol_version version = protocol_version::v1);

        /**
         * @brief Deserializes a protocol message to a stream-ready message.
         * @param message Protocol message to decode.
         * @return Decoded stream-ready message, or std::nullopt on type/payload mismatch.
         */
        [[nodiscard]] std::optional<stream_ready_message> deserialize_stream_ready(const protocol_message &message);

    } // namespace communication
} // namespace sf
