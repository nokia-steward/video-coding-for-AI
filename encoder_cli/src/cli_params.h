// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <gst/gst.h>
#include <memory>
#include <optional>
#include <string>

#include "encoder_enums.h"
#include "libvcai/include/area_copy.h"
#include "libvcai/include/communication/transport.h"

/**
 * @file cli_params.height
 * @brief Defines the structure and parsing logic for command-line parameters.
 */

/**
 * @struct command_line_parameters
 * @brief Holds all configuration parameters parsed from the command line.
 * Command line options override values from the configuration file.
 */
struct command_line_parameters {
    // The configuration file path is required.
    std::unique_ptr<gchar, decltype(&g_free)> config_file{nullptr, &g_free};

    struct common_cli_config {
        std::optional<input_type> input_type_value{};
        std::optional<std::string> output_directory{};
    };

    struct video_cli_config {
        std::optional<std::string> file_name{};
        std::optional<sf::video_format> format{};
        std::optional<int> width{};
        std::optional<int> height{};
        std::optional<int> framerate{};
        std::optional<uint32_t> background_color{};
    };

    struct camera_cli_config {
        std::optional<std::string> device{};
    };

    struct udp_cli_config {
        std::optional<std::string> bind_address{};
        std::optional<uint16_t> port{};
        std::optional<std::string> encoding{};
        std::optional<uint32_t> payload_type{};
        std::optional<uint32_t> clock_rate{};
        std::optional<int> latency_ms{};
    };

    struct roi_cli_config {
        std::optional<std::string> deepstream_config_file{};
        std::optional<std::string> labels{};
        std::optional<std::string> data_file{};
        std::optional<std::string> static_roi_json_file{};
        std::optional<std::string> groundtruth_roi_file{};
        std::optional<bool> inference_engine{};
        std::optional<bool> pack_rois{};
        std::optional<packer_type> packer_type_value{};
        std::optional<int> num_frames{};
        std::optional<int> roi_fixed_margin_px{};
        std::optional<roi_margin_mode> roi_margin_mode_value{};
        std::optional<int> temporal_resampling{};
        std::optional<background_mode> background_mode_value{};
        std::optional<int> qp_map_background_delta{};
        std::optional<bool> enable_luma_adaptation{};
        std::optional<double> luma_downscale_factor{};
        std::optional<bool> enable_adaptive_luma_adaptation{};
        std::optional<std::string> input_analyzer_model_path{};
        std::optional<bool> adaptive_luma_analyzer_on_keyframes_only{};
        std::optional<double> sigma{};
        std::optional<int> kernel_size{};
    };

    struct tracker_cli_config {
        std::optional<std::string> config_file{};
        std::optional<std::string> type{};
        std::optional<int> max_shadow_tracking_age{};
        std::optional<bool> enable_reassoc{};
        std::optional<int> trajectory_projection_length{};
        std::optional<int> max_tracklet_matching_time_range{};
    };

    struct detection_cli_config {
        std::optional<float> base_confidence_threshold{};
        std::optional<int> maximum_missing_frames{};
        std::optional<bool> enable_prediction{};
    };

    struct stabilization_cli_config {
        std::optional<bool> enabled{};
        std::optional<stabilization_mode> mode{};
        std::optional<float> alpha{};
        std::optional<float> alpha_size{};
        std::optional<int> min_position_delta{};
        std::optional<int> min_size_delta{};
        std::optional<float> velocity_adaptive_factor{};
    };

    struct encoder_cli_config {
        std::optional<int> qp{};
        std::optional<std::string> element{};
    };

    struct debug_cli_config {
        std::optional<bool> draw_borders{};
        std::optional<bool> draw_rtp_timestamp{};
        std::optional<bool> draw_fps{};
        std::optional<bool> draw_bitrate{};
        std::optional<bool> draw_bitrate_large{};
        std::optional<bool> save_frames{};
        std::optional<bool> enable_display_branch{};
        std::optional<bool> enable_mp4_branch{};
        std::optional<bool> enable_h265_branch{};
        bool debug_all = false;
    };

    struct logging_cli_config {
        std::optional<bool> enable_status_reporter{};
        std::optional<bool> packer_logging{};
    };

    struct communication_cli_config {
        std::optional<sf::communication::transport_type> transport_type{};
        std::optional<std::string> host{};
        std::optional<uint16_t> port{};
        std::optional<uint32_t> timeout_ms{};
    };

    struct webrtc_cli_config {
        std::optional<std::string> signaling_url{};
        std::optional<std::string> stun_server{};
        std::optional<std::string> turn_server{};
        std::optional<std::string> turn_username{};
        std::optional<std::string> turn_password{};
        std::optional<uint32_t> ice_timeout_ms{};
        std::optional<bool> enable_data_channels{};
    };

    common_cli_config common{};
    video_cli_config video{};
    camera_cli_config camera{};
    udp_cli_config udp{};
    roi_cli_config roi{};
    tracker_cli_config tracker{};
    detection_cli_config detection{};
    stabilization_cli_config stabilization{};
    encoder_cli_config encoder{};
    debug_cli_config debug{};
    logging_cli_config logging{};
    communication_cli_config communication{};
    webrtc_cli_config webrtc{};
};

/**
 * @brief Parses command-line arguments to get all configuration parameters.
 *
 * This function uses GLib's GOptionContext to define and parse command-line arguments.
 * It handles the --config/-c option to specify the configuration file path and optional
 * parameters for all configuration sections using the <group>-<option> naming system.
 * Command line options override values from the configuration file.
 * It will exit the program with an error message if parsing fails.
 *
 * @param argc The argument count from main().
 * @param argv The argument vector from main().
 * @return A struct containing all parsed configuration parameters.
 * @note This function will exit the program on parsing errors.
 */
[[nodiscard]] command_line_parameters parse_command_line(int argc, char **argv);
