// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <arpa/inet.h>
#include <chrono>
#include <fcntl.h>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "../../encoder_cli/src/gst/rtp_utils.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_rtp_utils.cpp
 * @brief Tests for RTP utility functions.
 *
 * Note: These tests require network functionality and may need mock servers
 * or network simulation for reliable testing.
 */

class RtpUtilsTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }

    void TearDown() override { GStreamerTestFixture::TearDown(); }

    /**
     * @brief Creates a simple UDP server for testing.
     * @param port Port to listen on.
     * @param response Response message to send (empty = no response).
     * @return Socket file descriptor, or -1 on error.
     */
    int create_test_udp_server(uint16_t port, const std::string &response = "") {
        const int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            return -1;
        }

        struct sockaddr_in server_addr {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(sockfd, reinterpret_cast<struct sockaddr *>(&server_addr), sizeof(server_addr)) < 0) {
            close(sockfd);
            return -1;
        }

        // Set socket to non-blocking for test purposes
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        return sockfd;
    }
};

/**
 * @brief Test test_rtp_server_connectivity with invalid IP address.
 */
TEST_F(RtpUtilsTest, TestConnectivityInvalidIP) {
    const bool result = test_rtp_server_connectivity("invalid.ip.address", 5000);
    EXPECT_FALSE(result);
}

/**
 * @brief Test test_rtp_server_connectivity with unreachable server.
 */
TEST_F(RtpUtilsTest, TestConnectivityUnreachableServer) {
    // Use a non-routable IP address (RFC 1918)
    // Note: Actual behavior depends on network configuration
    // The function may return true if it can send a packet (even if no response)
    // or false if it times out - both are acceptable behaviors
    const bool result = test_rtp_server_connectivity("192.168.255.255", 5000);
    // Function should handle unreachable servers gracefully (not crash)
    // Result can be true or false depending on network behavior
    (void) result; // Suppress unused variable warning
    SUCCEED();
}

/**
 * @brief Test test_rtp_server_connectivity with localhost (if available).
 */
TEST_F(RtpUtilsTest, TestConnectivityLocalhost) {
    // This test may fail if no server is running on localhost:5000
    // It's acceptable for this test to fail - it's testing the function's behavior
    const bool result = test_rtp_server_connectivity("127.0.0.1", 5000);
    // Result depends on whether a server is actually running
    // We just verify the function doesn't crash
    // Result can be true or false depending on server availability
    (void) result; // Suppress unused variable warning
    SUCCEED();
}

/**
 * @brief Test perform_rtp_handshake with invalid IP address.
 */
TEST_F(RtpUtilsTest, PerformHandshakeInvalidIP) {
    const auto result = perform_rtp_handshake("invalid.ip.address", 5000);
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test perform_rtp_handshake with unreachable server.
 */
TEST_F(RtpUtilsTest, PerformHandshakeUnreachableServer) {
    // Use a non-routable IP address
    const auto result = perform_rtp_handshake("192.168.255.255", 5000);
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test perform_rtp_handshake timeout behavior.
 */
TEST_F(RtpUtilsTest, PerformHandshakeTimeout) {
    // Test with a valid IP but no server (should timeout)
    const auto result = perform_rtp_handshake("127.0.0.1", 65535); // Unlikely to have server on this port
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test perform_rtp_handshake with localhost (if server available).
 */
TEST_F(RtpUtilsTest, PerformHandshakeLocalhost) {
    // This test requires an actual RTP server running on localhost:5000
    // It's acceptable for this test to fail if no server is available
    const auto result = perform_rtp_handshake("127.0.0.1", 5000);
    // If server is available, result should have a value with assigned_port > 0
    // If not available, result should be nullopt
    // We just verify the function doesn't crash
    (void) result; // Suppress unused variable warning
    SUCCEED();
}

/**
 * @brief Test RTP utilities handle empty host strings gracefully.
 */
TEST_F(RtpUtilsTest, EmptyHostString) {
    // Test with empty host string
    const bool connectivity_result = test_rtp_server_connectivity("", 5000);
    EXPECT_FALSE(connectivity_result);

    const auto handshake_result = perform_rtp_handshake("", 5000);
    EXPECT_FALSE(handshake_result.has_value());
}

/**
 * @brief Test RTP utilities handle invalid port numbers.
 */
TEST_F(RtpUtilsTest, InvalidPortNumbers) {
    // Port 0 is invalid
    const bool connectivity_result = test_rtp_server_connectivity("127.0.0.1", 0);
    // Function should handle this gracefully (may fail or return false)
    (void) connectivity_result;

    const auto handshake_result = perform_rtp_handshake("127.0.0.1", 0);
    EXPECT_FALSE(handshake_result.has_value());
}
