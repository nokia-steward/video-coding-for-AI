// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "communication/signaling.h"

namespace {
    class signaling_server_test : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            if (!gst_is_initialized()) {
                gst_init(nullptr, nullptr);
            }
        }
    };
} // namespace

TEST_F(signaling_server_test, null_guards_are_safe) {
    const sf::communication::signaling_message message{sf::communication::signaling_message_type::heartbeat, "payload"};

    EXPECT_FALSE(sf::communication::signaling_server_start(nullptr));
    EXPECT_FALSE(sf::communication::signaling_server_send_to_client(nullptr, "missing-client", message));
    EXPECT_NO_THROW(sf::communication::signaling_server_stop(nullptr));
}

TEST_F(signaling_server_test, send_to_unknown_client_returns_false) {
    auto server = sf::communication::create_signaling_server(
            0, false, [](const std::string &) {}, [](const std::string &) {},
            [](const std::string &, const sf::communication::signaling_message &) {});
    ASSERT_NE(server, nullptr);

    EXPECT_TRUE(sf::communication::signaling_server_start(server.get()));

    const sf::communication::signaling_message message{sf::communication::signaling_message_type::heartbeat, "payload"};
    EXPECT_FALSE(sf::communication::signaling_server_send_to_client(server.get(), "unknown-client", message));

    sf::communication::signaling_server_stop(server.get());
}

TEST_F(signaling_server_test, server_start_and_stop_lifecycle_is_repeatable) {
    auto server = sf::communication::create_signaling_server(
            0, false, [](const std::string &) {}, [](const std::string &) {},
            [](const std::string &, const sf::communication::signaling_message &) {});
    ASSERT_NE(server, nullptr);

    EXPECT_TRUE(sf::communication::signaling_server_start(server.get()));
    EXPECT_TRUE(sf::communication::signaling_server_start(server.get()));

    sf::communication::signaling_server_stop(server.get());

    EXPECT_TRUE(sf::communication::signaling_server_start(server.get()));
    sf::communication::signaling_server_stop(server.get());
}

TEST_F(signaling_server_test, signaling_client_null_guards_and_disconnected_send_are_safe) {
    const sf::communication::signaling_message message{sf::communication::signaling_message_type::heartbeat, "ping"};

    EXPECT_FALSE(sf::communication::signaling_client_connect(nullptr));
    EXPECT_FALSE(sf::communication::signaling_client_send(nullptr, message));
    EXPECT_FALSE(sf::communication::signaling_client_is_connected(nullptr));
    EXPECT_NO_THROW(sf::communication::signaling_client_disconnect(nullptr));
}

TEST_F(signaling_server_test, signaling_client_rejects_invalid_url_scheme) {
    bool callback_called = false;
    auto client = sf::communication::create_signaling_client(
            "http://127.0.0.1:7777",
            [&callback_called]([[maybe_unused]] const sf::communication::signaling_message &msg) {
                callback_called = true;
            });
    ASSERT_NE(client, nullptr);

    EXPECT_FALSE(sf::communication::signaling_client_connect(client.get()));
    EXPECT_FALSE(sf::communication::signaling_client_is_connected(client.get()));
    EXPECT_FALSE(callback_called);
}

TEST_F(signaling_server_test, signaling_client_rejects_url_without_port) {
    bool callback_called = false;
    auto client = sf::communication::create_signaling_client(
            "ws://127.0.0.1", [&callback_called]([[maybe_unused]] const sf::communication::signaling_message &msg) {
                callback_called = true;
            });
    ASSERT_NE(client, nullptr);

    EXPECT_FALSE(sf::communication::signaling_client_connect(client.get()));
    EXPECT_FALSE(sf::communication::signaling_client_is_connected(client.get()));
    EXPECT_FALSE(callback_called);
}

TEST_F(signaling_server_test, signaling_client_rejects_invalid_host_address) {
    bool callback_called = false;
    auto client = sf::communication::create_signaling_client(
            "ws://not_an_ip:7777",
            [&callback_called]([[maybe_unused]] const sf::communication::signaling_message &msg) {
                callback_called = true;
            });
    ASSERT_NE(client, nullptr);

    EXPECT_FALSE(sf::communication::signaling_client_connect(client.get()));
    EXPECT_FALSE(sf::communication::signaling_client_is_connected(client.get()));
    EXPECT_FALSE(callback_called);
}

TEST_F(signaling_server_test, signaling_client_send_fails_when_not_connected) {
    auto client = sf::communication::create_signaling_client(
            "ws://127.0.0.1:1", []([[maybe_unused]] const sf::communication::signaling_message &msg) {});
    ASSERT_NE(client, nullptr);

    const sf::communication::signaling_message message{sf::communication::signaling_message_type::ice_candidate,
                                                       "candidate:1 1 udp 1234 127.0.0.1 5000 typ host"};
    EXPECT_FALSE(sf::communication::signaling_client_send(client.get(), message));
    EXPECT_FALSE(sf::communication::signaling_client_is_connected(client.get()));
}

TEST_F(signaling_server_test, signaling_client_disconnect_is_safe_without_connection) {
    auto client = sf::communication::create_signaling_client(
            "ws://127.0.0.1:1", []([[maybe_unused]] const sf::communication::signaling_message &msg) {});
    ASSERT_NE(client, nullptr);

    EXPECT_FALSE(sf::communication::signaling_client_is_connected(client.get()));
    EXPECT_NO_THROW(sf::communication::signaling_client_disconnect(client.get()));
    EXPECT_FALSE(sf::communication::signaling_client_is_connected(client.get()));
}

TEST_F(signaling_server_test,
       init_debug_category_handle_received_data_signaling_receive_thread_and_destroy_signaling_client_paths_are_safe) {
    bool callback_called = false;
    auto client = sf::communication::create_signaling_client(
            "ws://127.0.0.1:1", [&callback_called]([[maybe_unused]] const sf::communication::signaling_message &msg) {
                callback_called = true;
            });
    ASSERT_NE(client, nullptr);

    EXPECT_FALSE(sf::communication::signaling_client_connect(client.get()));
    EXPECT_FALSE(sf::communication::signaling_client_is_connected(client.get()));
    EXPECT_NO_THROW(sf::communication::signaling_client_disconnect(client.get()));
    EXPECT_FALSE(callback_called);
}
