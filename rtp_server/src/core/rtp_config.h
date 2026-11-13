// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include <string>

/**
 * @file rtp_config.h
 * @brief Configuration structure for RTP server monitoring parameters and output options.
 */

/**
 * @enum transport_type
 * @brief Defines communication transport types.
 */
enum class transport_type { rtp, webrtc };

/**
 * @enum webrtc_signaling_mode
 * @brief Defines WebRTC signaling server modes.
 */
enum class webrtc_signaling_mode { embedded, external };

/**
 * @brief Converts transport_type enum to string.
 * @param type The transport type enum value.
 * @return String representation ("rtp" or "webrtc").
 */
[[nodiscard]] inline const char *transport_type_to_string(transport_type type) {
    switch (type) {
        case transport_type::rtp:
            return "rtp";
        case transport_type::webrtc:
            return "webrtc";
        default:
            return "rtp"; // Default fallback
    }
}

/**
 * @brief Converts string to transport_type enum.
 * @param type_str The transport type string ("rtp" or "webrtc").
 * @return The transport_type enum value, or rtp if string is not recognized.
 */
[[nodiscard]] inline transport_type string_to_transport_type(const std::string &type_str) {
    if (type_str == "webrtc") {
        return transport_type::webrtc;
    }
    return transport_type::rtp; // Default
}

/**
 * @brief Converts webrtc_signaling_mode enum to string.
 * @param mode The signaling mode enum value.
 * @return String representation ("embedded" or "external").
 */
[[nodiscard]] inline const char *webrtc_signaling_mode_to_string(webrtc_signaling_mode mode) {
    switch (mode) {
        case webrtc_signaling_mode::embedded:
            return "embedded";
        case webrtc_signaling_mode::external:
            return "external";
        default:
            return "embedded"; // Default fallback
    }
}

/**
 * @brief Converts string to webrtc_signaling_mode enum.
 * @param mode_str The signaling mode string ("embedded" or "external").
 * @return The webrtc_signaling_mode enum value, or embedded if string is not recognized.
 */
[[nodiscard]] inline webrtc_signaling_mode string_to_webrtc_signaling_mode(const std::string &mode_str) {
    if (mode_str == "external") {
        return webrtc_signaling_mode::external;
    }
    return webrtc_signaling_mode::embedded; // Default
}

/**
 * @struct rtp_monitoring_config
 * @brief Configuration parameters for RTP monitoring and quality checks.
 */
struct rtp_monitoring_config {
    // Monitoring enable/disable flags
    bool enable_packet_loss_check = false; ///< Enable packet loss monitoring
    bool enable_jitter_check = false; ///< Enable jitter monitoring
    bool enable_silence_check = false; ///< Enable silence detection monitoring

    // Quality thresholds (only used when respective checks are enabled)
    double max_packet_loss_rate = 5.0; ///< Maximum packet loss rate (percentage)
    double max_jitter_ms = 50.0; ///< Maximum jitter (milliseconds)
    uint64_t max_silence_ms = 10000; ///< Maximum silence duration (milliseconds)

    /**
     * @brief Default constructor with monitoring disabled by default.
     */
    rtp_monitoring_config() = default;
};

/**
 * @struct rtp_output_config
 * @brief Configuration parameters for RTP output forwarding.
 */
struct rtp_output_config {
    bool enabled = false; ///< Enable RTP output forwarding (disabled by default)
    std::string host = "127.0.0.1"; ///< RTP output destination host (default: localhost)
    guint16 port = 5004; ///< RTP output destination port (default: 5004)
    guint payload_type = 96; ///< RTP payload type for forwarded H.265 stream (default: 96)

    /**
     * @brief Default constructor with RTP output forwarding disabled by default.
     */
    rtp_output_config() = default;
};
