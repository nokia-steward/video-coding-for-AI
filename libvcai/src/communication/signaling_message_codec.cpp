// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "signaling_message_codec.h"

namespace sf::communication {
    const char *signaling_message_type_to_string(const signaling_message_type type) {
        switch (type) {
            case signaling_message_type::sdp_offer:
                return "sdp-offer";
            case signaling_message_type::sdp_answer:
                return "sdp-answer";
            case signaling_message_type::ice_candidate:
                return "ice-candidate";
            case signaling_message_type::handshake:
                return "handshake";
            case signaling_message_type::heartbeat:
                return "heartbeat";
            case signaling_message_type::disconnect:
                return "disconnect";
        }
        return nullptr;
    }

    std::optional<signaling_message_type> signaling_message_type_from_string(const std::string &type_string) {
        if (type_string == "sdp-offer") {
            return signaling_message_type::sdp_offer;
        }
        if (type_string == "sdp-answer") {
            return signaling_message_type::sdp_answer;
        }
        if (type_string == "ice-candidate") {
            return signaling_message_type::ice_candidate;
        }
        if (type_string == "handshake") {
            return signaling_message_type::handshake;
        }
        if (type_string == "heartbeat") {
            return signaling_message_type::heartbeat;
        }
        if (type_string == "disconnect") {
            return signaling_message_type::disconnect;
        }
        return std::nullopt;
    }

    std::optional<signaling_message> parse_signaling_message(const std::string &message_text) {
        const size_t colon_pos = message_text.find(':');
        if (colon_pos == std::string::npos) {
            return std::nullopt;
        }

        const std::string type_string = message_text.substr(0, colon_pos);
        const std::string data_string = message_text.substr(colon_pos + 1);
        const auto parsed_type = signaling_message_type_from_string(type_string);
        if (!parsed_type) {
            return std::nullopt;
        }

        return signaling_message{*parsed_type, data_string};
    }

    std::optional<std::string> format_signaling_message(const signaling_message &message) {
        const char *type_string = signaling_message_type_to_string(message.type);
        if (!type_string) {
            return std::nullopt;
        }

        return std::string(type_string) + ":" + message.data;
    }
} // namespace sf::communication
