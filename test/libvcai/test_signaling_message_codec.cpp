// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <vector>

#include "../../libvcai/src/communication/signaling_message_codec.h"

struct signaling_message_codec_test : ::testing::Test {};

TEST_F(signaling_message_codec_test, parse_valid_messages) {
    using sf::communication::signaling_message_type;

    const std::vector<std::pair<std::string, signaling_message_type>> test_cases = {
            {"sdp-offer", signaling_message_type::sdp_offer},
            {"sdp-answer", signaling_message_type::sdp_answer},
            {"ice-candidate", signaling_message_type::ice_candidate},
            {"handshake", signaling_message_type::handshake},
            {"heartbeat", signaling_message_type::heartbeat},
            {"disconnect", signaling_message_type::disconnect}};

    for (const auto &[message_type_token, expected_type]: test_cases) {
        const std::string wire_message = message_type_token + ":payload";
        const auto parsed_message = sf::communication::parse_signaling_message(wire_message);
        ASSERT_TRUE(parsed_message.has_value());
        EXPECT_EQ(parsed_message->type, expected_type);
        EXPECT_EQ(parsed_message->data, "payload");
    }
}

TEST_F(signaling_message_codec_test, reject_malformed_messages) {
    EXPECT_FALSE(sf::communication::parse_signaling_message("missing-colon").has_value());
    EXPECT_FALSE(sf::communication::parse_signaling_message("unknown-type:payload").has_value());
    EXPECT_FALSE(sf::communication::parse_signaling_message(":payload").has_value());
}

TEST_F(signaling_message_codec_test, format_and_parse_round_trip) {
    using sf::communication::signaling_message;
    using sf::communication::signaling_message_type;

    const std::vector<signaling_message> test_cases = {{signaling_message_type::sdp_offer, "offer-sdp"},
                                                       {signaling_message_type::sdp_answer, "answer-sdp"},
                                                       {signaling_message_type::ice_candidate, "candidate:with:colon"},
                                                       {signaling_message_type::handshake, "handshake-data"},
                                                       {signaling_message_type::heartbeat, "session=12"},
                                                       {signaling_message_type::disconnect, "disconnect-now"}};

    for (const auto &expected_message: test_cases) {
        const auto formatted_message = sf::communication::format_signaling_message(expected_message);
        ASSERT_TRUE(formatted_message.has_value());

        const auto parsed_message = sf::communication::parse_signaling_message(*formatted_message);
        ASSERT_TRUE(parsed_message.has_value());
        EXPECT_EQ(parsed_message->type, expected_message.type);
        EXPECT_EQ(parsed_message->data, expected_message.data);
    }
}

TEST_F(signaling_message_codec_test, reject_unknown_message_type_on_format) {
    const auto invalid_type = static_cast<sf::communication::signaling_message_type>(255);
    const sf::communication::signaling_message message{invalid_type, "data"};
    EXPECT_FALSE(sf::communication::format_signaling_message(message).has_value());
}
