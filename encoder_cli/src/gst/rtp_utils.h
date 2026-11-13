// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

/**
 * @file rtp_utils.height
 * @brief Header for RTP-related utility functions.
 */

/**
 * @brief Initializes the RTP utils debug category.
 */
void init_rtp_utils_debug_category();

/**
 * @brief Tests basic connectivity to the RTP server.
 * @param server_host The RTP server host address.
 * @param server_port The RTP server port for handshake.
 * @return true if server is reachable, false otherwise.
 */
[[nodiscard]] bool test_rtp_server_connectivity(const std::string &server_host, uint16_t server_port);

/**
 * @brief Handshake result for RTP communication setup.
 */
struct rtp_handshake_result {
    uint16_t assigned_port = 0;
    uint32_t session_id = 0;
};

/**
 * @brief Performs communication handshake with the server to get a streaming port.
 * @param server_host The communication server host address.
 * @param server_port The communication server port for handshake.
 * @param transport_type The transport type ("rtp" or "webrtc").
 * @param webrtc_signaling_url WebRTC signaling URL (only used when transport_type="webrtc").
 * @return The handshake result, or std::nullopt if handshake failed.
 */
[[nodiscard]] std::optional<rtp_handshake_result> perform_rtp_handshake(const std::string &server_host,
                                                                        uint16_t server_port,
                                                                        const std::string &transport_type = "rtp",
                                                                        const std::string &webrtc_signaling_url = "");

/**
 * @brief Sends a disconnect request and waits for server ack.
 * @param server_host The communication server host address.
 * @param server_port The communication server port for handshake.
 * @param transport_type The transport type ("rtp" or "webrtc").
 * @param webrtc_signaling_url WebRTC signaling URL (only used when transport_type="webrtc").
 * @param streaming_port The streaming port assigned by handshake.
 * @param session_id The session identifier assigned by handshake.
 * @return true if ack received, false otherwise.
 */
[[nodiscard]] bool send_rtp_disconnect(const std::string &server_host, uint16_t server_port,
                                       const std::string &transport_type, const std::string &webrtc_signaling_url,
                                       uint16_t streaming_port, uint32_t session_id);
