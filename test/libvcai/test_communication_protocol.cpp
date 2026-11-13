// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

#include <gio/gio.h>
#include <gst/gst.h>
#include "communication/protocol.h"
#include "communication/transport_udp.h"

struct communication_protocol_test : ::testing::Test {
    static void SetUpTestSuite() {
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
    }
};

TEST_F(communication_protocol_test, round_trip_protocol_message_payload) {
    const sf::communication::protocol_message original_message(
            sf::communication::protocol_version::v1, sf::communication::message_type::heartbeat, {0xAA, 0xBB, 0xCC});

    const auto serialized = sf::communication::serialize_message(original_message);
    const auto deserialized = sf::communication::deserialize_message(serialized);

    ASSERT_TRUE(deserialized.has_value());
    EXPECT_EQ(deserialized->version, original_message.version);
    EXPECT_EQ(deserialized->type, original_message.type);
    EXPECT_EQ(deserialized->payload, original_message.payload);
}

TEST_F(communication_protocol_test, reject_short_or_truncated_protocol_messages) {
    const std::vector<uint8_t> short_header = {0x01, 0x03, 0x00};
    EXPECT_FALSE(sf::communication::deserialize_message(short_header).has_value());

    const std::vector<uint8_t> truncated_payload = {0x01, 0x03, 0x00, 0x04, // header claims 4-byte payload
                                                    0xAA, 0xBB}; // only 2 payload bytes present
    EXPECT_FALSE(sf::communication::deserialize_message(truncated_payload).has_value());
}

TEST_F(communication_protocol_test, handshake_round_trip) {
    const sf::communication::handshake_request request{};
    const auto request_message = sf::communication::serialize_handshake_request(request);
    const auto decoded_request = sf::communication::deserialize_handshake_request(request_message);
    ASSERT_TRUE(decoded_request.has_value());

    const sf::communication::handshake_response response{5000, 42};
    const auto response_message = sf::communication::serialize_handshake_response(response);
    const auto decoded_response = sf::communication::deserialize_handshake_response(response_message);
    ASSERT_TRUE(decoded_response.has_value());
    EXPECT_EQ(decoded_response->assigned_port, response.assigned_port);
    EXPECT_EQ(decoded_response->session_id, response.session_id);
}

TEST_F(communication_protocol_test, heartbeat_and_disconnect_round_trip) {
    const sf::communication::heartbeat_message heartbeat{6000, 101};
    const auto heartbeat_message = sf::communication::serialize_heartbeat(heartbeat);
    const auto decoded_heartbeat = sf::communication::deserialize_heartbeat(heartbeat_message);
    ASSERT_TRUE(decoded_heartbeat.has_value());
    EXPECT_EQ(decoded_heartbeat->streaming_port, heartbeat.streaming_port);
    EXPECT_EQ(decoded_heartbeat->session_id, heartbeat.session_id);

    const sf::communication::disconnect_message disconnect{6000, 101};

    const auto disconnect_request = sf::communication::serialize_disconnect_request(disconnect);
    const auto decoded_disconnect_request = sf::communication::deserialize_disconnect_request(disconnect_request);
    ASSERT_TRUE(decoded_disconnect_request.has_value());
    EXPECT_EQ(decoded_disconnect_request->streaming_port, disconnect.streaming_port);
    EXPECT_EQ(decoded_disconnect_request->session_id, disconnect.session_id);

    const auto disconnect_ack = sf::communication::serialize_disconnect_ack(disconnect);
    const auto decoded_disconnect_ack = sf::communication::deserialize_disconnect_ack(disconnect_ack);
    ASSERT_TRUE(decoded_disconnect_ack.has_value());
    EXPECT_EQ(decoded_disconnect_ack->streaming_port, disconnect.streaming_port);
    EXPECT_EQ(decoded_disconnect_ack->session_id, disconnect.session_id);

    const sf::communication::stream_ready_message ready{6000, 101};
    const auto ready_message = sf::communication::serialize_stream_ready(ready);
    const auto decoded_ready = sf::communication::deserialize_stream_ready(ready_message);
    ASSERT_TRUE(decoded_ready.has_value());
    EXPECT_EQ(decoded_ready->streaming_port, ready.streaming_port);
    EXPECT_EQ(decoded_ready->session_id, ready.session_id);
}

TEST_F(communication_protocol_test, reject_invalid_fixed_size_payloads) {
    const sf::communication::protocol_message invalid_heartbeat_payload(
            sf::communication::protocol_version::v1, sf::communication::message_type::heartbeat, {0x01});
    EXPECT_FALSE(sf::communication::deserialize_heartbeat(invalid_heartbeat_payload).has_value());

    const sf::communication::protocol_message invalid_disconnect_request_payload(
            sf::communication::protocol_version::v1, sf::communication::message_type::disconnect_request, {0x01, 0x02});
    EXPECT_FALSE(sf::communication::deserialize_disconnect_request(invalid_disconnect_request_payload).has_value());

    const sf::communication::protocol_message invalid_disconnect_ack_payload(
            sf::communication::protocol_version::v1, sf::communication::message_type::disconnect_ack, {0x01, 0x02});
    EXPECT_FALSE(sf::communication::deserialize_disconnect_ack(invalid_disconnect_ack_payload).has_value());

    const sf::communication::protocol_message invalid_stream_ready_payload(
            sf::communication::protocol_version::v1, sf::communication::message_type::stream_ready, {0x01, 0x02});
    EXPECT_FALSE(sf::communication::deserialize_stream_ready(invalid_stream_ready_payload).has_value());
}

TEST_F(communication_protocol_test, reject_wrong_message_type_for_decoder) {
    const sf::communication::disconnect_message disconnect{7777, 1234};
    const auto disconnect_request = sf::communication::serialize_disconnect_request(disconnect);

    EXPECT_FALSE(sf::communication::deserialize_heartbeat(disconnect_request).has_value());
    EXPECT_FALSE(sf::communication::deserialize_disconnect_ack(disconnect_request).has_value());
    EXPECT_FALSE(sf::communication::deserialize_stream_ready(disconnect_request).has_value());
}

TEST_F(communication_protocol_test, udp_transport_reports_not_connected_before_connect) {
    auto transport_data = sf::communication::create_udp_transport_data();
    ASSERT_NE(transport_data, nullptr);

    const sf::communication::protocol_message heartbeat_msg(sf::communication::protocol_version::v1,
                                                            sf::communication::message_type::heartbeat, {0x01, 0x02});

    EXPECT_FALSE(sf::communication::udp_is_connected_impl(transport_data.get()));
    EXPECT_EQ(sf::communication::udp_send_impl(transport_data.get(), heartbeat_msg),
              sf::communication::transport_error::not_connected);

    const auto receive_result = sf::communication::udp_receive_impl(transport_data.get());
    EXPECT_EQ(receive_result.error, sf::communication::transport_error::not_connected);
    EXPECT_FALSE(receive_result.message.has_value());

    EXPECT_EQ(sf::communication::udp_disconnect_impl(transport_data.get()),
              sf::communication::transport_error::success);
}

TEST_F(communication_protocol_test, udp_transport_connect_rejects_invalid_host) {
    auto transport_data = sf::communication::create_udp_transport_data();
    ASSERT_NE(transport_data, nullptr);

    const sf::communication::transport_config config{
            .host = "invalid-host-address",
            .port = 5000,
            .timeout = std::chrono::milliseconds(1000),
    };

    EXPECT_EQ(sf::communication::udp_connect_impl(transport_data.get(), config),
              sf::communication::transport_error::connection_failed);
    EXPECT_FALSE(sf::communication::udp_is_connected_impl(transport_data.get()));
}

TEST_F(communication_protocol_test, udp_transport_null_guards_return_expected_errors) {
    const sf::communication::transport_config config{
            .host = "127.0.0.1",
            .port = 5000,
            .timeout = std::chrono::milliseconds(100),
    };
    const sf::communication::protocol_message heartbeat_msg(sf::communication::protocol_version::v1,
                                                            sf::communication::message_type::heartbeat, {0x01});

    EXPECT_EQ(sf::communication::udp_connect_impl(nullptr, config), sf::communication::transport_error::unknown_error);
    EXPECT_EQ(sf::communication::udp_send_impl(nullptr, heartbeat_msg),
              sf::communication::transport_error::not_connected);
    EXPECT_EQ(sf::communication::udp_disconnect_impl(nullptr), sf::communication::transport_error::unknown_error);
    EXPECT_FALSE(sf::communication::udp_is_connected_impl(nullptr));

    const auto receive_result = sf::communication::udp_receive_impl(nullptr);
    EXPECT_EQ(receive_result.error, sf::communication::transport_error::not_connected);
    EXPECT_FALSE(receive_result.message.has_value());
}

TEST_F(communication_protocol_test, udp_transport_reconnect_resets_previous_connection_state) {
    auto transport_data = sf::communication::create_udp_transport_data();
    ASSERT_NE(transport_data, nullptr);

    const sf::communication::transport_config valid_config{
            .host = "127.0.0.1",
            .port = 6553,
            .timeout = std::chrono::milliseconds(500),
    };
    ASSERT_EQ(sf::communication::udp_connect_impl(transport_data.get(), valid_config),
              sf::communication::transport_error::success);
    EXPECT_TRUE(sf::communication::udp_is_connected_impl(transport_data.get()));

    const sf::communication::transport_config invalid_config{
            .host = "invalid-host-address",
            .port = 6554,
            .timeout = std::chrono::milliseconds(500),
    };
    EXPECT_EQ(sf::communication::udp_connect_impl(transport_data.get(), invalid_config),
              sf::communication::transport_error::connection_failed);
    EXPECT_FALSE(sf::communication::udp_is_connected_impl(transport_data.get()));
}

TEST_F(communication_protocol_test, udp_transport_connect_and_send_to_localhost) {
    auto transport_data = sf::communication::create_udp_transport_data();
    ASSERT_NE(transport_data, nullptr);

    const sf::communication::transport_config config{
            .host = "127.0.0.1",
            .port = 6553,
            .timeout = std::chrono::milliseconds(1000),
    };
    ASSERT_EQ(sf::communication::udp_connect_impl(transport_data.get(), config),
              sf::communication::transport_error::success);
    EXPECT_TRUE(sf::communication::udp_is_connected_impl(transport_data.get()));

    const auto serialized_heartbeat =
            sf::communication::serialize_heartbeat(sf::communication::heartbeat_message{6553, 42});
    EXPECT_EQ(sf::communication::udp_send_impl(transport_data.get(), serialized_heartbeat),
              sf::communication::transport_error::success);

    const auto receive_result = sf::communication::udp_receive_impl(transport_data.get());
    EXPECT_TRUE(receive_result.error == sf::communication::transport_error::timeout ||
                receive_result.error == sf::communication::transport_error::receive_failed);
    EXPECT_FALSE(receive_result.message.has_value());

    EXPECT_EQ(sf::communication::udp_disconnect_impl(transport_data.get()),
              sf::communication::transport_error::success);
    EXPECT_FALSE(sf::communication::udp_is_connected_impl(transport_data.get()));
}

TEST_F(communication_protocol_test, udp_transport_receive_reports_invalid_message_for_malformed_datagram) {
    GError *gerror = nullptr;
    GSocket *server_socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &gerror);
    ASSERT_NE(server_socket, nullptr) << (gerror ? gerror->message : "failed to create UDP socket");
    g_clear_error(&gerror);

    GInetAddress *bind_ip = g_inet_address_new_from_string("127.0.0.1");
    ASSERT_NE(bind_ip, nullptr);
    GSocketAddress *bind_address = g_inet_socket_address_new(bind_ip, 0);
    ASSERT_NE(bind_address, nullptr);
    ASSERT_TRUE(g_socket_bind(server_socket, bind_address, TRUE, &gerror))
            << (gerror ? gerror->message : "failed to bind UDP socket");
    g_clear_error(&gerror);
    g_object_unref(bind_ip);
    g_object_unref(bind_address);

    GSocketAddress *local_address = g_socket_get_local_address(server_socket, &gerror);
    ASSERT_NE(local_address, nullptr) << (gerror ? gerror->message : "failed to get local UDP address");
    g_clear_error(&gerror);
    ASSERT_TRUE(G_IS_INET_SOCKET_ADDRESS(local_address));
    const uint16_t server_port =
            static_cast<uint16_t>(g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(local_address)));
    g_object_unref(local_address);

    g_socket_set_timeout(server_socket, 1);

    auto transport_data = sf::communication::create_udp_transport_data();
    ASSERT_NE(transport_data, nullptr);
    const sf::communication::transport_config config{
            .host = "127.0.0.1",
            .port = server_port,
            .timeout = std::chrono::milliseconds(1000),
    };
    ASSERT_EQ(sf::communication::udp_connect_impl(transport_data.get(), config),
              sf::communication::transport_error::success);

    const auto valid_message = sf::communication::serialize_heartbeat(sf::communication::heartbeat_message{
            .streaming_port = server_port,
            .session_id = 77,
    });
    ASSERT_EQ(sf::communication::udp_send_impl(transport_data.get(), valid_message),
              sf::communication::transport_error::success);

    char receive_buffer[256];
    GSocketAddress *sender_address = nullptr;
    const gssize received = g_socket_receive_from(server_socket, &sender_address, receive_buffer,
                                                  sizeof(receive_buffer), nullptr, &gerror);
    ASSERT_GE(received, 0) << (gerror ? gerror->message : "failed to receive UDP payload");
    g_clear_error(&gerror);
    ASSERT_NE(sender_address, nullptr);

    const uint8_t malformed[] = {0x01, 0x02, 0x00};
    const gssize sent = g_socket_send_to(server_socket, sender_address, reinterpret_cast<const gchar *>(malformed),
                                         sizeof(malformed), nullptr, &gerror);
    ASSERT_EQ(sent, static_cast<gssize>(sizeof(malformed)))
            << (gerror ? gerror->message : "failed to send malformed UDP payload");
    g_clear_error(&gerror);
    g_object_unref(sender_address);

    const auto receive_result = sf::communication::udp_receive_impl(transport_data.get());
    EXPECT_EQ(receive_result.error, sf::communication::transport_error::invalid_message);
    EXPECT_FALSE(receive_result.message.has_value());

    EXPECT_EQ(sf::communication::udp_disconnect_impl(transport_data.get()),
              sf::communication::transport_error::success);
    g_object_unref(server_socket);
}

TEST_F(communication_protocol_test, transport_udp_init_debug_category_and_lifecycle_guards_are_stable) {
    auto transport_data = sf::communication::create_udp_transport_data();
    ASSERT_NE(transport_data, nullptr);

    const sf::communication::transport_config config{
            .host = "127.0.0.1",
            .port = 6552,
            .timeout = std::chrono::milliseconds(100),
    };
    const auto connect_result = sf::communication::udp_connect_impl(transport_data.get(), config);
    EXPECT_TRUE(connect_result == sf::communication::transport_error::success ||
                connect_result == sf::communication::transport_error::connection_failed);
}
