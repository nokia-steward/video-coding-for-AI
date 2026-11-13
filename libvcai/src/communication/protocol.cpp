// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/communication/protocol.h"

#include <cstring>
#include <optional>

namespace sf::communication {
    namespace {
        constexpr size_t MESSAGE_HEADER_SIZE = 4; // version(1) + type(1) + payload_length(2)
        constexpr size_t PORT_AND_SESSION_ID_PAYLOAD_SIZE = 6; // streaming_port(2) + session_id(4)

        /**
         * @brief Writes a 16-bit value in network byte order (big-endian).
         * @param value The value to write.
         * @param buffer The buffer to write to (must have at least 2 bytes).
         */
        void write_uint16_be(const uint16_t value, uint8_t *buffer) {
            buffer[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
            buffer[1] = static_cast<uint8_t>(value & 0xFF);
        }

        /**
         * @brief Reads a 16-bit value from network byte order (big-endian).
         * @param buffer The buffer to read from (must have at least 2 bytes).
         * @return The 16-bit value.
         */
        [[nodiscard]] uint16_t read_uint16_be(const uint8_t *buffer) {
            return static_cast<uint16_t>((static_cast<uint16_t>(buffer[0]) << 8) | static_cast<uint16_t>(buffer[1]));
        }

        /**
         * @brief Writes a 32-bit value in network byte order (big-endian).
         * @param value The value to write.
         * @param buffer The buffer to write to (must have at least 4 bytes).
         */
        void write_uint32_be(const uint32_t value, uint8_t *buffer) {
            buffer[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
            buffer[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
            buffer[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
            buffer[3] = static_cast<uint8_t>(value & 0xFF);
        }

        /**
         * @brief Reads a 32-bit value from network byte order (big-endian).
         * @param buffer The buffer to read from (must have at least 4 bytes).
         * @return The 32-bit value.
         */
        [[nodiscard]] uint32_t read_uint32_be(const uint8_t *buffer) {
            return (static_cast<uint32_t>(buffer[0]) << 24) | (static_cast<uint32_t>(buffer[1]) << 16) |
                   (static_cast<uint32_t>(buffer[2]) << 8) | static_cast<uint32_t>(buffer[3]);
        }

        struct port_and_session_id_payload {
            uint16_t streaming_port;
            uint32_t session_id;
        };

        /**
         * @brief Serializes fixed-size payload [streaming_port:2][session_id:4].
         * @param streaming_port Streaming port value.
         * @param session_id Session identifier value.
         * @return Serialized payload bytes.
         */
        [[nodiscard]] std::vector<uint8_t> serialize_port_and_session_id_payload(const uint16_t streaming_port,
                                                                                 const uint32_t session_id) {
            std::vector<uint8_t> payload(PORT_AND_SESSION_ID_PAYLOAD_SIZE);
            write_uint16_be(streaming_port, payload.data());
            write_uint32_be(session_id, payload.data() + 2);
            return payload;
        }

        /**
         * @brief Deserializes fixed-size payload [streaming_port:2][session_id:4].
         * @param message Protocol message containing payload.
         * @param expected_type Expected message type.
         * @return Parsed payload fields, or std::nullopt on type/size mismatch.
         */
        [[nodiscard]] std::optional<port_and_session_id_payload>
        deserialize_port_and_session_id_payload(const protocol_message &message, const message_type expected_type) {
            if (message.type != expected_type) {
                return std::nullopt;
            }
            if (message.payload.size() != PORT_AND_SESSION_ID_PAYLOAD_SIZE) {
                return std::nullopt;
            }

            return port_and_session_id_payload{read_uint16_be(message.payload.data()),
                                               read_uint32_be(message.payload.data() + 2)};
        }
    } // namespace

    std::vector<uint8_t> serialize_message(const protocol_message &message) {
        const uint16_t payload_length = static_cast<uint16_t>(message.payload.size());
        std::vector<uint8_t> result;
        result.reserve(MESSAGE_HEADER_SIZE + payload_length);

        result.push_back(static_cast<uint8_t>(message.version));
        result.push_back(static_cast<uint8_t>(message.type));
        result.resize(MESSAGE_HEADER_SIZE);
        write_uint16_be(payload_length, result.data() + 2);
        result.insert(result.end(), message.payload.begin(), message.payload.end());

        return result;
    }

    std::optional<protocol_message> deserialize_message(const std::vector<uint8_t> &data) {
        if (data.size() < MESSAGE_HEADER_SIZE) {
            return std::nullopt;
        }

        const protocol_version version = static_cast<protocol_version>(data[0]);
        const message_type type = static_cast<message_type>(data[1]);
        const uint16_t payload_length = read_uint16_be(data.data() + 2);

        if (data.size() < MESSAGE_HEADER_SIZE + payload_length) {
            return std::nullopt;
        }

        std::vector<uint8_t> payload(data.begin() + MESSAGE_HEADER_SIZE,
                                     data.begin() + MESSAGE_HEADER_SIZE + payload_length);

        return protocol_message(version, type, std::move(payload));
    }

    protocol_message serialize_handshake_request(const handshake_request & /* request */, protocol_version version) {
        // Handshake request currently has no payload
        return protocol_message(version, message_type::handshake_request, std::vector<uint8_t>());
    }

    std::optional<handshake_request> deserialize_handshake_request(const protocol_message &message) {
        if (message.type != message_type::handshake_request) {
            return std::nullopt;
        }

        // Handshake request currently has no payload, so payload should be empty
        if (!message.payload.empty()) {
            return std::nullopt;
        }

        return handshake_request{};
    }

    protocol_message serialize_handshake_response(const handshake_response &response, protocol_version version) {
        std::vector<uint8_t> payload;
        payload.reserve(6); // port(2) + session_id(4)

        payload.resize(2);
        write_uint16_be(response.assigned_port, payload.data());

        payload.resize(6);
        write_uint32_be(response.session_id, payload.data() + 2);

        return protocol_message(version, message_type::handshake_response, std::move(payload));
    }

    std::optional<handshake_response> deserialize_handshake_response(const protocol_message &message) {
        if (message.type != message_type::handshake_response) {
            return std::nullopt;
        }

        if (message.payload.size() != PORT_AND_SESSION_ID_PAYLOAD_SIZE) {
            return std::nullopt;
        }

        const uint16_t assigned_port = read_uint16_be(message.payload.data());
        const uint32_t session_id = read_uint32_be(message.payload.data() + 2);

        return handshake_response{assigned_port, session_id};
    }

    protocol_message serialize_heartbeat(const heartbeat_message &heartbeat, protocol_version version) {
        return protocol_message(version, message_type::heartbeat,
                                serialize_port_and_session_id_payload(heartbeat.streaming_port, heartbeat.session_id));
    }

    std::optional<heartbeat_message> deserialize_heartbeat(const protocol_message &message) {
        if (message.type != message_type::heartbeat) {
            return std::nullopt;
        }

        const auto payload = deserialize_port_and_session_id_payload(message, message_type::heartbeat);
        if (!payload) {
            return std::nullopt;
        }
        return heartbeat_message{payload->streaming_port, payload->session_id};
    }

    protocol_message serialize_disconnect_request(const disconnect_message &disconnect, protocol_version version) {
        return protocol_message(
                version, message_type::disconnect_request,
                serialize_port_and_session_id_payload(disconnect.streaming_port, disconnect.session_id));
    }

    std::optional<disconnect_message> deserialize_disconnect_request(const protocol_message &message) {
        if (message.type != message_type::disconnect_request) {
            return std::nullopt;
        }

        const auto payload = deserialize_port_and_session_id_payload(message, message_type::disconnect_request);
        if (!payload) {
            return std::nullopt;
        }
        return disconnect_message{payload->streaming_port, payload->session_id};
    }

    protocol_message serialize_disconnect_ack(const disconnect_message &disconnect, protocol_version version) {
        return protocol_message(
                version, message_type::disconnect_ack,
                serialize_port_and_session_id_payload(disconnect.streaming_port, disconnect.session_id));
    }

    std::optional<disconnect_message> deserialize_disconnect_ack(const protocol_message &message) {
        if (message.type != message_type::disconnect_ack) {
            return std::nullopt;
        }

        const auto payload = deserialize_port_and_session_id_payload(message, message_type::disconnect_ack);
        if (!payload) {
            return std::nullopt;
        }
        return disconnect_message{payload->streaming_port, payload->session_id};
    }

    protocol_message serialize_stream_ready(const stream_ready_message &ready, protocol_version version) {
        return protocol_message(version, message_type::stream_ready,
                                serialize_port_and_session_id_payload(ready.streaming_port, ready.session_id));
    }

    std::optional<stream_ready_message> deserialize_stream_ready(const protocol_message &message) {
        if (message.type != message_type::stream_ready) {
            return std::nullopt;
        }

        const auto payload = deserialize_port_and_session_id_payload(message, message_type::stream_ready);
        if (!payload) {
            return std::nullopt;
        }
        return stream_ready_message{payload->streaming_port, payload->session_id};
    }

} // namespace sf::communication
