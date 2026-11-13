// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <string>

#include "../../include/communication/signaling.h"

namespace sf::communication {
    /**
     * @brief Converts a signaling message type to its wire-format string token.
     * @param type Message type.
     * @return Wire-format token, or nullptr if type is unknown.
     */
    [[nodiscard]] const char *signaling_message_type_to_string(signaling_message_type type);

    /**
     * @brief Converts a wire-format string token to signaling message type.
     * @param type_string Message type token from wire payload.
     * @return Parsed message type, or std::nullopt if unknown.
     */
    [[nodiscard]] std::optional<signaling_message_type>
    signaling_message_type_from_string(const std::string &type_string);

    /**
     * @brief Parses a signaling message from wire format.
     * @param message_text Wire-format message in "TYPE:data" form.
     * @return Parsed signaling message, or std::nullopt on malformed input.
     */
    [[nodiscard]] std::optional<signaling_message> parse_signaling_message(const std::string &message_text);

    /**
     * @brief Formats a signaling message to wire format.
     * @param message Message to format.
     * @return Wire-format message in "TYPE:data" form, or std::nullopt for unknown message type.
     */
    [[nodiscard]] std::optional<std::string> format_signaling_message(const signaling_message &message);
} // namespace sf::communication
