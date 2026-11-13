// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "area_copy.h"
#include "rtp_config.h"
#include "rtp_server_config.h"

/**
 * @file configuration_manager.h
 * @brief Manages configuration loading and command line parameter overrides.
 */

/**
 * @brief Initialize the configuration manager debug category.
 */
void init_configuration_manager_debug_category();

/**
 * @struct command_line_parameters
 * @brief Holds command line parameters that may override config file values.
 */
struct command_line_parameters {
    std::string config_file = "rtp_server_config.ini"; ///< Path to configuration file
    std::optional<guint16> handshake_port; ///< Handshake port override

    // Common parameters
    std::optional<sf::background_color> background_color; ///< Background color override
    std::optional<int> video_width; ///< Video width override
    std::optional<int> video_height; ///< Video height override

    // Monitoring parameters
    std::optional<bool> enable_packet_loss_check;
    std::optional<bool> enable_jitter_check;
    std::optional<bool> enable_silence_check;
    std::optional<double> max_packet_loss_rate;
    std::optional<double> max_jitter_ms;
    std::optional<uint64_t> max_silence_ms;

    // RTP output parameters
    std::optional<bool> enable_rtp_output;
    std::optional<std::string> rtp_output_host;
    std::optional<guint16> rtp_output_port;
    std::optional<guint> rtp_output_payload_type;

    // Output directory parameters
    std::optional<std::string> output_directory;

    // Inference parameters
    std::optional<bool> inference_engine;
    std::optional<std::string> deepstream_config_file;
    std::optional<std::string> labels;

    // Test mode parameters
    std::optional<std::string> groundtruth_file;
    std::optional<std::string> semseg_groundtruth_file;
    std::optional<double> test_mode_iou_threshold;
    std::optional<std::string> test_mode_output_md;
    std::optional<std::string> test_mode_output_json;
    std::optional<std::string> semseg_output_md;
    std::optional<std::string> semseg_output_json;

    // Tracker parameters
    std::optional<bool> tracker_enabled;
    std::optional<std::string> tracker_config_file;
    std::optional<std::string> tracker_ll_lib_file;

    // Border parameters
    std::optional<bool> draw_borders;
    std::optional<uint32_t> border_width;

    // Communication transport parameters
    std::optional<transport_type> communication_transport_type;

    // WebRTC parameters (only used when transport_type=webrtc)
    std::optional<uint16_t> webrtc_signaling_port;
    std::optional<bool> webrtc_signaling_use_ssl;
    std::optional<std::string> webrtc_signaling_url;
    std::optional<std::string> webrtc_stun_server;
    std::optional<std::string> webrtc_turn_server;
    std::optional<std::string> webrtc_turn_username;
    std::optional<std::string> webrtc_turn_password;
    std::optional<uint32_t> webrtc_ice_timeout_ms;

    // Debug parameters
    std::optional<bool> draw_bitrate;
    std::optional<bool> draw_bitrate_large;
    std::optional<bool> draw_fps;
    std::optional<bool> save_frames;

    // TET parameters
    std::optional<bool> tet_enabled;
    std::optional<std::string> tet_tile_file;
    std::optional<uint32_t> tet_qp;
    std::optional<bool> tet_dynamic_qp;

    // Decoder parameters
    std::optional<std::string> decoder_element;
    std::optional<bool> enable_luma_backscale;
};

/**
 * @brief Loads and configures the RTP server with command line parameter overrides.
 *
 * This function loads the RTP server configuration from a file and applies any
 * command line parameter overrides. It also creates a default configuration
 * file if one doesn't exist.
 *
 * @param params The command line parameters that may override config file values.
 * @return The fully configured RTP server configuration.
 * @throws std::runtime_error if configuration loading fails.
 */
[[nodiscard]] rtp_server_config load_and_configure_rtp_server(const command_line_parameters &params);
