// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <optional>

#include "../../libvcai/include/communication/manager.h"
#include "../../libvcai/include/communication/transport_webrtc.h"
#include "../../libvcai/src/communication/manager_test_hooks.h"

namespace {
    using sf::communication::disconnect_message;
    using sf::communication::handshake_response;
    using sf::communication::manager_create_transport_fn;
    using sf::communication::manager_transport_connect_fn;
    using sf::communication::manager_transport_disconnect_fn;
    using sf::communication::manager_transport_get_type_fn;
    using sf::communication::manager_transport_receive_fn;
    using sf::communication::manager_transport_send_fn;
    using sf::communication::manager_transport_set_webrtc_config_fn;
    using sf::communication::protocol_message;
    using sf::communication::transport_config;
    using sf::communication::transport_error;
    using sf::communication::transport_handle;
    using sf::communication::transport_receive_result;
    using sf::communication::transport_type;

    struct fake_transport_state {
        bool create_returns_null = false;
        transport_type transport_kind = transport_type::udp;
        transport_error connect_result = transport_error::success;
        transport_error send_result = transport_error::success;
        transport_receive_result receive_result = transport_receive_result::failure(transport_error::not_connected);
        transport_error disconnect_result = transport_error::success;
        int create_calls = 0;
        int set_webrtc_config_calls = 0;
        int connect_calls = 0;
        int send_calls = 0;
        int receive_calls = 0;
        int disconnect_calls = 0;
        transport_type last_create_type = transport_type::udp;
        std::optional<protocol_message> last_sent_message = std::nullopt;
    };

    fake_transport_state *g_fake_transport_state = nullptr;

    void fake_destroy_transport(transport_handle *handle) { std::unique_ptr<int>{reinterpret_cast<int *>(handle)}; }

    std::unique_ptr<transport_handle, void (*)(transport_handle *)> fake_create_transport(const transport_type type) {
        if (g_fake_transport_state == nullptr) {
            return {nullptr, fake_destroy_transport};
        }
        g_fake_transport_state->create_calls++;
        g_fake_transport_state->last_create_type = type;
        if (g_fake_transport_state->create_returns_null) {
            return {nullptr, fake_destroy_transport};
        }

        return {reinterpret_cast<transport_handle *>(new int(1)), fake_destroy_transport};
    }

    transport_type fake_transport_get_type([[maybe_unused]] const transport_handle *handle) {
        return g_fake_transport_state ? g_fake_transport_state->transport_kind : transport_type::udp;
    }

    void fake_transport_set_webrtc_config([[maybe_unused]] transport_handle *handle,
                                          [[maybe_unused]] const sf::communication::webrtc_config &webrtc_config) {
        if (g_fake_transport_state) {
            g_fake_transport_state->set_webrtc_config_calls++;
        }
    }

    transport_error fake_transport_connect([[maybe_unused]] transport_handle *handle,
                                           [[maybe_unused]] const transport_config &config) {
        if (!g_fake_transport_state) {
            return transport_error::unknown_error;
        }
        g_fake_transport_state->connect_calls++;
        return g_fake_transport_state->connect_result;
    }

    transport_error fake_transport_send([[maybe_unused]] transport_handle *handle, const protocol_message &message) {
        if (!g_fake_transport_state) {
            return transport_error::unknown_error;
        }
        g_fake_transport_state->send_calls++;
        g_fake_transport_state->last_sent_message = message;
        return g_fake_transport_state->send_result;
    }

    transport_receive_result fake_transport_receive([[maybe_unused]] transport_handle *handle) {
        if (!g_fake_transport_state) {
            return transport_receive_result::failure(transport_error::unknown_error);
        }
        g_fake_transport_state->receive_calls++;
        return g_fake_transport_state->receive_result;
    }

    transport_error fake_transport_disconnect([[maybe_unused]] transport_handle *handle) {
        if (!g_fake_transport_state) {
            return transport_error::unknown_error;
        }
        g_fake_transport_state->disconnect_calls++;
        return g_fake_transport_state->disconnect_result;
    }

    [[nodiscard]] transport_config make_transport_config() {
        return transport_config{
                .host = "127.0.0.1",
                .port = 5000,
                .timeout = std::chrono::milliseconds(200),
        };
    }
} // namespace

class communication_manager_test : public ::testing::Test {
protected:
    void SetUp() override {
        g_fake_transport_state = &state;
        sf::communication::manager_test_set_transport_hooks(
                static_cast<manager_create_transport_fn>(fake_create_transport),
                static_cast<manager_transport_get_type_fn>(fake_transport_get_type),
                static_cast<manager_transport_set_webrtc_config_fn>(fake_transport_set_webrtc_config),
                static_cast<manager_transport_connect_fn>(fake_transport_connect),
                static_cast<manager_transport_send_fn>(fake_transport_send),
                static_cast<manager_transport_receive_fn>(fake_transport_receive),
                static_cast<manager_transport_disconnect_fn>(fake_transport_disconnect));
    }

    void TearDown() override {
        sf::communication::manager_test_reset_transport_hooks();
        g_fake_transport_state = nullptr;
    }

    fake_transport_state state{};
};

TEST_F(communication_manager_test, create_manager_returns_null_when_transport_creation_fails) {
    state.create_returns_null = true;
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    EXPECT_EQ(manager, nullptr);
    EXPECT_EQ(state.create_calls, 1);
}

TEST_F(communication_manager_test, set_webrtc_config_calls_transport_only_for_webrtc_transport) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);

    const sf::communication::webrtc_config webrtc_cfg{
            .signaling_url = "ws://127.0.0.1:5000",
            .stun_server = "",
            .turn_server = "",
            .turn_username = "",
            .turn_password = "",
            .ice_timeout_ms = 1000,
            .enable_data_channels = false,
    };

    state.transport_kind = transport_type::udp;
    sf::communication::manager_set_webrtc_config(manager.get(), webrtc_cfg);
    EXPECT_EQ(state.set_webrtc_config_calls, 0);

    state.transport_kind = transport_type::webrtc;
    sf::communication::manager_set_webrtc_config(manager.get(), webrtc_cfg);
    EXPECT_EQ(state.set_webrtc_config_calls, 1);
}

TEST_F(communication_manager_test, manager_initialize_propagates_connect_error) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);

    state.connect_result = transport_error::connection_failed;
    const auto result = sf::communication::manager_initialize(manager.get(), make_transport_config());
    EXPECT_EQ(result, transport_error::connection_failed);
    EXPECT_EQ(state.connect_calls, 1);
}

TEST_F(communication_manager_test, manager_perform_handshake_returns_success_payload) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(sf::communication::manager_initialize(manager.get(), make_transport_config()), transport_error::success);

    state.receive_result = transport_receive_result::success(
            sf::communication::serialize_handshake_response(handshake_response{5004, 77}));

    const auto result = sf::communication::manager_perform_handshake(manager.get());
    EXPECT_EQ(result.error, transport_error::success);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->assigned_port, 5004);
    EXPECT_EQ(result.result->session_id, 77U);
    ASSERT_TRUE(state.last_sent_message.has_value());
    EXPECT_EQ(state.last_sent_message->type, sf::communication::message_type::handshake_request);
}

TEST_F(communication_manager_test, manager_perform_handshake_returns_invalid_message_on_receive_failure) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(sf::communication::manager_initialize(manager.get(), make_transport_config()), transport_error::success);

    state.receive_result = transport_receive_result::failure(transport_error::timeout);

    const auto result = sf::communication::manager_perform_handshake(manager.get());
    EXPECT_EQ(result.error, transport_error::invalid_message);
    EXPECT_FALSE(result.result.has_value());
}

TEST_F(communication_manager_test, wait_for_disconnect_ack_rejects_mismatched_ack) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(sf::communication::manager_initialize(manager.get(), make_transport_config()), transport_error::success);

    state.receive_result = transport_receive_result::success(
            sf::communication::serialize_disconnect_ack(disconnect_message{5001, 123}));

    const auto result = sf::communication::manager_wait_for_disconnect_ack(manager.get(), 5004, 999);
    EXPECT_EQ(result, transport_error::invalid_message);
}

TEST_F(communication_manager_test, wait_for_stream_ready_returns_success_for_matching_message) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(sf::communication::manager_initialize(manager.get(), make_transport_config()), transport_error::success);

    state.receive_result = transport_receive_result::success(
            sf::communication::serialize_stream_ready(sf::communication::stream_ready_message{5004, 999}));

    const auto result = sf::communication::manager_wait_for_stream_ready(manager.get(), 5004, 999);
    EXPECT_EQ(result, transport_error::success);
}

TEST_F(communication_manager_test, wait_for_stream_ready_propagates_timeout_errors) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(sf::communication::manager_initialize(manager.get(), make_transport_config()), transport_error::success);

    state.receive_result = transport_receive_result::failure(transport_error::timeout);

    const auto result = sf::communication::manager_wait_for_stream_ready(manager.get(), 5004, 999);
    EXPECT_EQ(result, transport_error::timeout);
}

TEST_F(communication_manager_test, wait_for_stream_ready_rejects_mismatched_message_payload) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(sf::communication::manager_initialize(manager.get(), make_transport_config()), transport_error::success);

    state.receive_result = transport_receive_result::success(
            sf::communication::serialize_stream_ready(sf::communication::stream_ready_message{5001, 123}));

    const auto result = sf::communication::manager_wait_for_stream_ready(manager.get(), 5004, 999);
    EXPECT_EQ(result, transport_error::invalid_message);
}

TEST_F(communication_manager_test, shutdown_disconnects_once_and_is_idempotent) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);
    ASSERT_EQ(sf::communication::manager_initialize(manager.get(), make_transport_config()), transport_error::success);

    EXPECT_EQ(sf::communication::manager_shutdown(manager.get()), transport_error::success);
    EXPECT_EQ(state.disconnect_calls, 1);

    EXPECT_EQ(sf::communication::manager_shutdown(manager.get()), transport_error::success);
    EXPECT_EQ(state.disconnect_calls, 1);
}

TEST_F(communication_manager_test, send_operations_require_initialized_manager) {
    const auto manager = sf::communication::create_communication_manager(transport_type::udp);
    ASSERT_NE(manager, nullptr);

    EXPECT_EQ(sf::communication::manager_send_heartbeat(manager.get(), 5004, 7), transport_error::not_connected);
    EXPECT_EQ(sf::communication::manager_send_disconnect(manager.get(), 5004, 7), transport_error::not_connected);
}
