// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "configuration_manager.h"

#include <gst/gst.h>

#include "../gst/gpu_elements.h"
#include "logging.h"
#include "rtp_server_config.h"

// Debug category for configuration management.
GST_DEBUG_CATEGORY_STATIC(configuration_manager_category);
#define GST_CAT_DEFAULT configuration_manager_category

void init_configuration_manager_debug_category() {
    GST_DEBUG_CATEGORY_INIT(configuration_manager_category, "configuration-manager", 0, "Configuration Management");
}

/**
 * @file configuration_manager.cpp
 * @brief Implementation of configuration management and command line parameter overrides.
 */

namespace {
    /**
     * @brief Applies common parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_common_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.background_color.has_value()) {
            config.background_color = params.background_color.value();
            sf::info(configuration_manager_category, "Background color set from command line: %u",
                     config.background_color);
        }
        if (params.video_width.has_value()) {
            config.video_width = params.video_width.value();
            sf::info(configuration_manager_category, "Video width set from command line: %d", config.video_width);
        }
        if (params.video_height.has_value()) {
            config.video_height = params.video_height.value();
            sf::info(configuration_manager_category, "Video height set from command line: %d", config.video_height);
        }
    }

    /**
     * @brief Applies server parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_server_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.handshake_port.has_value() && params.handshake_port.value() > 0) {
            config.handshake_port = params.handshake_port.value();
            sf::info(configuration_manager_category, "Handshake port set from command line: %u",
                     params.handshake_port.value());
        }
    }

    /**
     * @brief Applies monitoring parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_monitoring_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.enable_packet_loss_check.has_value()) {
            config.monitoring_config.enable_packet_loss_check = params.enable_packet_loss_check.value();
            sf::info(configuration_manager_category, "Packet loss check set from command line: %s",
                     params.enable_packet_loss_check.value() ? "enabled" : "disabled");
        }

        if (params.enable_jitter_check.has_value()) {
            config.monitoring_config.enable_jitter_check = params.enable_jitter_check.value();
            sf::info(configuration_manager_category, "Jitter check set from command line: %s",
                     params.enable_jitter_check.value() ? "enabled" : "disabled");
        }

        if (params.enable_silence_check.has_value()) {
            config.monitoring_config.enable_silence_check = params.enable_silence_check.value();
            sf::info(configuration_manager_category, "Silence check set from command line: %s",
                     params.enable_silence_check.value() ? "enabled" : "disabled");
        }

        if (params.max_packet_loss_rate.has_value()) {
            config.monitoring_config.max_packet_loss_rate = params.max_packet_loss_rate.value();
            sf::info(configuration_manager_category, "Max packet loss rate set from command line: %.2f%%",
                     params.max_packet_loss_rate.value());
        }

        if (params.max_jitter_ms.has_value()) {
            config.monitoring_config.max_jitter_ms = params.max_jitter_ms.value();
            sf::info(configuration_manager_category, "Max jitter set from command line: %.2f ms",
                     params.max_jitter_ms.value());
        }

        if (params.max_silence_ms.has_value()) {
            config.monitoring_config.max_silence_ms = params.max_silence_ms.value();
            sf::info(configuration_manager_category, "Max silence set from command line: %lu ms",
                     params.max_silence_ms.value());
        }
    }

    /**
     * @brief Applies RTP output parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_rtp_output_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.enable_rtp_output.has_value()) {
            config.rtp_output.enabled = params.enable_rtp_output.value();
            sf::info(configuration_manager_category, "RTP output set from command line: %s",
                     params.enable_rtp_output.value() ? "enabled" : "disabled");
        }

        if (params.rtp_output_host.has_value()) {
            config.rtp_output.host = params.rtp_output_host.value();
            // Enable RTP output if host is specified
            config.rtp_output.enabled = true;
            sf::info(configuration_manager_category, "RTP output host set from command line: %s",
                     params.rtp_output_host.value().c_str());
        }

        if (params.rtp_output_port.has_value() && params.rtp_output_port.value() > 0) {
            config.rtp_output.port = params.rtp_output_port.value();
            // Enable RTP output if port is specified
            config.rtp_output.enabled = true;
            sf::info(configuration_manager_category, "RTP output port set from command line: %u",
                     params.rtp_output_port.value());
        }

        if (params.rtp_output_payload_type.has_value()) {
            config.rtp_output.payload_type = params.rtp_output_payload_type.value();
            // Enable RTP output if payload type is specified
            config.rtp_output.enabled = true;
            sf::info(configuration_manager_category, "RTP output payload type set from command line: %u",
                     params.rtp_output_payload_type.value());
        }
    }

    /**
     * @brief Applies output directory override from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_output_directory_override(rtp_server_config &config, const command_line_parameters &params) {
        if (params.output_directory.has_value()) {
            if (!params.output_directory.value().empty()) {
                config.output_directory = params.output_directory.value();
                sf::info(configuration_manager_category, "Output directory set from command line: %s",
                         params.output_directory.value().c_str());
            } else {
                config.output_directory = std::nullopt;
                sf::info(configuration_manager_category, "Output directory cleared from command line (no file saving)");
            }
        }
    }

    /**
     * @brief Applies inference parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_inference_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.inference_engine.has_value()) {
            config.inference_engine = params.inference_engine.value();
            sf::info(configuration_manager_category, "Inference engine set from command line: %s",
                     config.inference_engine ? "enabled" : "disabled");
        }

        if (params.deepstream_config_file.has_value()) {
            config.deepstream_config_file = params.deepstream_config_file.value();
            sf::info(configuration_manager_category, "DeepStream config file set from command line: %s",
                     params.deepstream_config_file.value().c_str());
        }

        if (params.labels.has_value()) {
            config.labels = params.labels.value();
            sf::info(configuration_manager_category, "Labels filter set from command line: %s",
                     params.labels.value().c_str());
        }
    }

    /**
     * @brief Applies test mode parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_test_mode_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.groundtruth_file.has_value()) {
            config.groundtruth_file = params.groundtruth_file.value();
            sf::info(configuration_manager_category, "Groundtruth file set from command line: %s",
                     params.groundtruth_file.value().c_str());
        }

        if (params.semseg_groundtruth_file.has_value()) {
            config.semseg_groundtruth_file = params.semseg_groundtruth_file.value();
            sf::info(configuration_manager_category, "SemSeg groundtruth file set from command line: %s",
                     params.semseg_groundtruth_file.value().c_str());
        }

        if (params.test_mode_iou_threshold.has_value()) {
            config.test_mode_iou_threshold = params.test_mode_iou_threshold.value();
            sf::info(configuration_manager_category, "Test mode IoU threshold set from command line: %.3f",
                     params.test_mode_iou_threshold.value());
        }

        if (params.test_mode_output_md.has_value()) {
            config.test_mode_output_md = params.test_mode_output_md.value();
            sf::info(configuration_manager_category, "Test mode markdown output set from command line: %s",
                     params.test_mode_output_md.value().c_str());
        }

        if (params.test_mode_output_json.has_value()) {
            config.test_mode_output_json = params.test_mode_output_json.value();
            sf::info(configuration_manager_category, "Test mode json output set from command line: %s",
                     params.test_mode_output_json.value().c_str());
        }

        if (params.semseg_output_md.has_value()) {
            config.semseg_output_md = params.semseg_output_md.value();
            sf::info(configuration_manager_category, "SemSeg markdown output set from command line: %s",
                     params.semseg_output_md.value().c_str());
        }

        if (params.semseg_output_json.has_value()) {
            config.semseg_output_json = params.semseg_output_json.value();
            sf::info(configuration_manager_category, "SemSeg json output set from command line: %s",
                     params.semseg_output_json.value().c_str());
        }
    }

    /**
     * @brief Applies tracker parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_tracker_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.tracker_enabled.has_value()) {
            config.tracker_enabled = params.tracker_enabled.value();
            sf::info(configuration_manager_category, "Tracker set from command line: %s",
                     config.tracker_enabled ? "enabled" : "disabled");
        }

        if (params.tracker_config_file.has_value()) {
            config.tracker_config_file = params.tracker_config_file.value();
            sf::info(configuration_manager_category, "Tracker config file set from command line: %s",
                     params.tracker_config_file.value().c_str());
        }

        if (params.tracker_ll_lib_file.has_value()) {
            config.tracker_ll_lib_file = params.tracker_ll_lib_file.value();
            sf::info(configuration_manager_category, "Tracker ll-lib-file set from command line: %s",
                     params.tracker_ll_lib_file.value().c_str());
        }
    }

    /**
     * @brief Applies border parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_border_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.draw_borders.has_value()) {
            config.draw_borders = params.draw_borders.value();
            sf::info(configuration_manager_category, "Draw borders set from command line: %s",
                     config.draw_borders ? "enabled" : "disabled");
        }

        if (params.border_width.has_value()) {
            config.border_width = params.border_width.value();
            sf::info(configuration_manager_category, "Border width set from command line: %u",
                     params.border_width.value());
        }
    }

    /**
     * @brief Applies communication transport type override from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_transport_type_override(rtp_server_config &config, const command_line_parameters &params) {
        if (params.communication_transport_type.has_value()) {
            config.communication_transport_type = params.communication_transport_type.value();
            sf::info(configuration_manager_category, "Transport type set from command line: %s",
                     transport_type_to_string(params.communication_transport_type.value()));
        }
    }

    /**
     * @brief Applies WebRTC parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_webrtc_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.webrtc_signaling_port.has_value()) {
            config.webrtc_signaling_port = params.webrtc_signaling_port.value();
            sf::info(configuration_manager_category, "WebRTC signaling port set from command line: %u",
                     params.webrtc_signaling_port.value());
        }
        if (params.webrtc_signaling_use_ssl.has_value()) {
            config.webrtc_signaling_use_ssl = params.webrtc_signaling_use_ssl.value();
            sf::info(configuration_manager_category, "WebRTC signaling SSL set from command line: %s",
                     params.webrtc_signaling_use_ssl.value() ? "enabled" : "disabled");
        }
        if (params.webrtc_signaling_url.has_value()) {
            config.webrtc_signaling_url = params.webrtc_signaling_url.value();
            sf::info(configuration_manager_category, "WebRTC signaling URL set from command line: %s",
                     params.webrtc_signaling_url.value().c_str());
        }
        if (params.webrtc_stun_server.has_value()) {
            config.webrtc_stun_server = params.webrtc_stun_server.value();
            sf::info(configuration_manager_category, "WebRTC STUN server set from command line: %s",
                     params.webrtc_stun_server.value().c_str());
        }
        if (params.webrtc_turn_server.has_value()) {
            config.webrtc_turn_server = params.webrtc_turn_server.value();
            sf::info(configuration_manager_category, "WebRTC TURN server set from command line: %s",
                     params.webrtc_turn_server.value().c_str());
        }
        if (params.webrtc_turn_username.has_value()) {
            config.webrtc_turn_username = params.webrtc_turn_username.value();
            sf::info(configuration_manager_category, "WebRTC TURN username set from command line");
        }
        if (params.webrtc_turn_password.has_value()) {
            config.webrtc_turn_password = params.webrtc_turn_password.value();
            sf::info(configuration_manager_category, "WebRTC TURN password set from command line");
        }
        if (params.webrtc_ice_timeout_ms.has_value()) {
            config.webrtc_ice_timeout_ms = params.webrtc_ice_timeout_ms.value();
            sf::info(configuration_manager_category, "WebRTC ICE timeout set from command line: %u ms",
                     params.webrtc_ice_timeout_ms.value());
        }
    }

    /**
     * @brief Applies debug parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_debug_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.draw_bitrate.has_value()) {
            config.draw_bitrate = params.draw_bitrate.value();
            sf::info(configuration_manager_category, "Draw bitrate set from command line: %s",
                     params.draw_bitrate.value() ? "enabled" : "disabled");
        }
        if (params.draw_bitrate_large.has_value()) {
            config.draw_bitrate_large = params.draw_bitrate_large.value();
            sf::info(configuration_manager_category, "Draw bitrate large set from command line: %s",
                     params.draw_bitrate_large.value() ? "enabled" : "disabled");
        }
        if (params.draw_fps.has_value()) {
            config.draw_fps = params.draw_fps.value();
            sf::info(configuration_manager_category, "FPS display overlay %s from command line",
                     params.draw_fps.value() ? "enabled" : "disabled");
        }
        if (params.save_frames.has_value()) {
            config.save_frames = params.save_frames.value();
            sf::info(configuration_manager_category, "Save frames set from command line: %s",
                     params.save_frames.value() ? "enabled" : "disabled");
        }
    }

    /**
     * @brief Applies TET parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_tet_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.tet_enabled.has_value()) {
            config.tet_enabled = params.tet_enabled.value();
            sf::info(configuration_manager_category, "TET enabled set from command line: %s",
                     params.tet_enabled.value() ? "yes" : "no");
        }
        if (params.tet_tile_file.has_value()) {
            config.tet_tile_file = params.tet_tile_file.value();
            sf::info(configuration_manager_category, "TET tile file set from command line: %s",
                     params.tet_tile_file.value().c_str());
        }
        if (params.tet_qp.has_value()) {
            config.tet_qp = params.tet_qp.value();
            sf::info(configuration_manager_category, "TET QP set from command line: %u", params.tet_qp.value());
        }
        if (params.tet_dynamic_qp.has_value()) {
            config.tet_dynamic_qp = params.tet_dynamic_qp.value();
            sf::info(configuration_manager_category, "TET dynamic QP set from command line: %s",
                     params.tet_dynamic_qp.value() ? "enabled" : "disabled");
        }
    }

    void apply_decoder_parameter_overrides(rtp_server_config &config, const command_line_parameters &params) {
        if (params.decoder_element.has_value()) {
            config.decoder_element = params.decoder_element.value();
            sf::info(configuration_manager_category, "Decoder element set from command line: %s",
                     params.decoder_element.value().c_str());
        }
        if (params.enable_luma_backscale.has_value()) {
            config.enable_luma_backscale = params.enable_luma_backscale.value();
            sf::info(configuration_manager_category, "Decoder luma backscale set from command line: %s",
                     params.enable_luma_backscale.value() ? "enabled" : "disabled");
        }
    }
} // namespace

rtp_server_config load_and_configure_rtp_server(const command_line_parameters &params) {
    // Check if config file exists, create default if it doesn't
    const std::filesystem::path config_path(params.config_file);
    if (!std::filesystem::exists(config_path)) {
        sf::info(configuration_manager_category, "Configuration file does not exist, creating default: %s",
                 config_path.c_str());
        create_default_rtp_server_config_file(config_path);
    }

    auto config = load_rtp_server_config(config_path);
    config.config_directory = std::filesystem::absolute(config_path).parent_path();

    // Apply command line parameter overrides grouped by category
    apply_common_parameter_overrides(config, params);
    apply_server_parameter_overrides(config, params);
    apply_monitoring_parameter_overrides(config, params);
    apply_rtp_output_parameter_overrides(config, params);
    apply_output_directory_override(config, params);
    apply_inference_parameter_overrides(config, params);
    apply_test_mode_parameter_overrides(config, params);
    apply_tracker_parameter_overrides(config, params);
    apply_border_parameter_overrides(config, params);
    apply_transport_type_override(config, params);
    apply_webrtc_parameter_overrides(config, params);
    apply_debug_parameter_overrides(config, params);
    apply_tet_parameter_overrides(config, params);
    apply_decoder_parameter_overrides(config, params);

    validate_decoder_element_or_throw(config);

    return config;
}
