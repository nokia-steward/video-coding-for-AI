// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include "communication/protocol.h"
#include "communication/transport.h"
#include "communication/transport_webrtc.h"

namespace {
    class transport_webrtc_test : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            if (!gst_is_initialized()) {
                gst_init(nullptr, nullptr);
            }
        }
    };
} // namespace

TEST_F(transport_webrtc_test, null_input_guards_return_expected_errors) {
    sf::communication::transport_config transport_cfg{
            .host = "127.0.0.1",
            .port = 5000,
            .timeout = std::chrono::milliseconds(200),
    };
    sf::communication::webrtc_config webrtc_cfg{
            .signaling_url = "ws://127.0.0.1:5000",
            .stun_server = "",
            .turn_server = "",
            .turn_username = "",
            .turn_password = "",
            .ice_timeout_ms = 1000,
            .enable_data_channels = false,
    };
    const sf::communication::protocol_message msg(sf::communication::protocol_version::v1,
                                                  sf::communication::message_type::heartbeat, {});

    EXPECT_EQ(sf::communication::webrtc_connect_impl(nullptr, transport_cfg, webrtc_cfg),
              sf::communication::transport_error::unknown_error);
    EXPECT_EQ(sf::communication::webrtc_send_impl(nullptr, msg), sf::communication::transport_error::not_connected);
    EXPECT_EQ(sf::communication::webrtc_disconnect_impl(nullptr), sf::communication::transport_error::unknown_error);
    EXPECT_FALSE(sf::communication::webrtc_is_connected_impl(nullptr));

    const auto receive_result = sf::communication::webrtc_receive_impl(nullptr);
    EXPECT_EQ(receive_result.error, sf::communication::transport_error::not_connected);
    EXPECT_FALSE(receive_result.message.has_value());
}

TEST_F(transport_webrtc_test, disconnected_transport_returns_not_connected_for_send_and_receive) {
    auto data = sf::communication::create_webrtc_transport_data();
    ASSERT_NE(data, nullptr);

    const sf::communication::protocol_message msg(sf::communication::protocol_version::v1,
                                                  sf::communication::message_type::heartbeat,
                                                  std::vector<uint8_t>{0x01, 0x02});

    EXPECT_FALSE(sf::communication::webrtc_is_connected_impl(data.get()));
    EXPECT_EQ(sf::communication::webrtc_send_impl(data.get(), msg), sf::communication::transport_error::not_connected);

    const auto receive_result = sf::communication::webrtc_receive_impl(data.get());
    EXPECT_EQ(receive_result.error, sf::communication::transport_error::not_connected);
    EXPECT_FALSE(receive_result.message.has_value());

    EXPECT_EQ(sf::communication::webrtc_disconnect_impl(data.get()), sf::communication::transport_error::success);
}

TEST_F(transport_webrtc_test, connect_returns_connection_failed_for_invalid_signaling_url) {
    auto data = sf::communication::create_webrtc_transport_data();
    ASSERT_NE(data, nullptr);

    const sf::communication::transport_config transport_cfg{
            .host = "127.0.0.1",
            .port = 6553,
            .timeout = std::chrono::milliseconds(50),
    };
    const sf::communication::webrtc_config bad_webrtc_cfg{
            .signaling_url = "invalid-url",
            .stun_server = "",
            .turn_server = "",
            .turn_username = "",
            .turn_password = "",
            .ice_timeout_ms = 50,
            .enable_data_channels = false,
    };

    EXPECT_EQ(sf::communication::webrtc_connect_impl(data.get(), transport_cfg, bad_webrtc_cfg),
              sf::communication::transport_error::connection_failed);
    EXPECT_FALSE(sf::communication::webrtc_is_connected_impl(data.get()));
}
