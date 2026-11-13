// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "area_copy.h"
#include "gst_raii.h"
#include "rtp_config.h"

/**
 * @file rtp_server_config.h
 * @brief Defines the structure and parsing logic for RTP server configuration.
 */

/**
 * @struct rtp_server_config
 * @brief Holds all configuration parameters for the RTP server.
 */
struct rtp_server_config {
    // --- Server Parameters ---
    guint16 handshake_port;

    // --- Communication Parameters ---
    // Transport type: rtp (default), webrtc, etc.
    transport_type communication_transport_type;

    // --- Common Parameters ---
    // Background color for empty buffers (default: green, which is 0 in YUV)
    sf::background_color background_color;
    // Video width/height for inference-related configuration (0 means unset).
    int video_width = 0;
    int video_height = 0;

    // --- Monitoring Parameters ---
    rtp_monitoring_config monitoring_config;

    // --- RTP Output Parameters ---
    rtp_output_config rtp_output;

    // --- Output Directory Parameters ---
    std::optional<std::string> output_directory; ///< Optional output directory for saving video files
    // Directory containing the active config file. Used as a fallback root for debug outputs.
    std::filesystem::path config_directory;

    // --- Inference Parameters ---
    // When enabled, the display branch will run DeepStream inference (nvinfer) using the provided config.
    bool inference_engine = false;
    // Path to DeepStream YAML config (e.g. ../yolo/yolo_config_11m.yml). Empty means no inference configuration.
    std::string deepstream_config_file;
    // Optional path to DeepStream YAML config dedicated to semantic segmentation.
    std::string semseg_deepstream_config_file;
    // Optional semantic-segmentation ONNX model path used by semseg config generation.
    std::string semseg_model_file;
    // Optional semantic-segmentation TensorRT engine path used by semseg config generation.
    std::string semseg_engine_file;
    // Optional semantic-segmentation labels file path used by semseg config generation.
    std::string semseg_labels_file;
    // Optional semantic-segmentation config profile suffix used during generation.
    std::string semseg_profile;
    // Optional comma-separated list of object labels to draw borders for (filtering). Empty means no filtering.
    std::string labels;

    // --- Test Mode Parameters ---
    // Path to COCO-style groundtruth JSON file for detection accuracy testing.
    std::string groundtruth_file;
    // Path to sparse semantic-segmentation groundtruth JSON file.
    std::string semseg_groundtruth_file;
    // IoU threshold used for matching detections to groundtruth.
    double test_mode_iou_threshold = 0.5;
    // Optional output file for final test mode summary (markdown).
    std::optional<std::string> test_mode_output_md;
    // Optional output file for final test mode summary (json).
    std::optional<std::string> test_mode_output_json;
    // Optional output file for final semantic-segmentation summary (markdown).
    std::optional<std::string> semseg_output_md;
    // Optional output file for final semantic-segmentation summary (json).
    std::optional<std::string> semseg_output_json;

    // --- Tracker Parameters ---
    // When enabled, the display branch will run DeepStream tracking (nvtracker).
    bool tracker_enabled = false;
    // Optional DeepStream tracker config file path. Empty means use plugin defaults.
    std::string tracker_config_file;
    // Low-level tracker library file used by nvtracker. Default points to the DeepStream symlink location.
    std::string tracker_ll_lib_file;

    // --- Border Parameters ---
    // Draw blue borders around detected objects (display only).
    bool draw_borders = false;
    // Border width in pixels.
    uint32_t border_width = 2;

    // --- Debug Parameters ---
    bool enable_display_sink = true; ///< Enable on-screen display sink output (nveglglessink)
    bool draw_bitrate = false; ///< Enable bitrate display overlay on decoded frames
    bool draw_bitrate_large =
            false; ///< Enable large red bitrate display overlay on decoded frames (2x size, red color)
    bool draw_fps = false; ///< Enable FPS display overlay on decoded frames
    bool save_frames = false; ///< Save final reconstructed frames as PNG files

    // --- TET Parameters ---
    bool tet_enabled = false;
    std::string tet_tile_file;
    uint32_t tet_qp = 27;
    bool tet_dynamic_qp = false;

    // --- Decoder Parameters ---
    /// GStreamer decoder factory name (optional). When unset, build default is used.
    /// Registered names only; unsupported names fail at startup.
    /// INI: [Decoder] element = <name>  CLI: --decoder-element <name>
    std::optional<std::string> decoder_element;
    /// Apply the EOI luma backscale (inverse of encode-side luma adaptation) after decode.
    /// Defaults on. INI: [Decoder] enable_luma_backscale  CLI: --decoder-enable-luma-backscale
    bool enable_luma_backscale = true;

    // --- WebRTC Parameters (only used when transport_type=webrtc) ---
    // Signaling mode: embedded (server acts as signaling server) or external (connect to external server)
    webrtc_signaling_mode webrtc_signaling_mode_enum;
    // Signaling port (for embedded mode)
    uint16_t webrtc_signaling_port;
    // Use SSL for signaling (for embedded mode)
    bool webrtc_signaling_use_ssl;
    // Signaling URL (for external mode)
    std::string webrtc_signaling_url;
    // STUN server URL (optional)
    std::string webrtc_stun_server;
    // TURN server URL (optional)
    std::string webrtc_turn_server;
    // TURN server username (optional)
    std::string webrtc_turn_username;
    // TURN server password (optional)
    std::string webrtc_turn_password;
    // ICE candidate timeout in milliseconds
    uint32_t webrtc_ice_timeout_ms;

    /**
     * @brief Default constructor with default values.
     */
    rtp_server_config() :
        handshake_port(5000), communication_transport_type(transport_type::rtp),
        background_color(sf::background_color::green), video_width(0), video_height(0), monitoring_config(),
        rtp_output(), output_directory(std::nullopt), config_directory("."), inference_engine(false),
        deepstream_config_file(), semseg_deepstream_config_file(), semseg_model_file(), semseg_engine_file(),
        semseg_labels_file(), semseg_profile(), labels(), groundtruth_file(), semseg_groundtruth_file(),
        test_mode_iou_threshold(0.5), test_mode_output_md(std::nullopt), test_mode_output_json(std::nullopt),
        semseg_output_md(std::nullopt), semseg_output_json(std::nullopt), tracker_enabled(false), tracker_config_file(),
        tracker_ll_lib_file("/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"),
        draw_borders(false), border_width(2), enable_display_sink(true), draw_bitrate(false), draw_fps(false),
        save_frames(false), tet_enabled(false), tet_tile_file(), tet_qp(27), tet_dynamic_qp(false),
        decoder_element(std::nullopt), enable_luma_backscale(true),
        webrtc_signaling_mode_enum(webrtc_signaling_mode::embedded), webrtc_signaling_port(8080),
        webrtc_signaling_use_ssl(false), webrtc_signaling_url(), webrtc_stun_server(), webrtc_turn_server(),
        webrtc_turn_username(), webrtc_turn_password(), webrtc_ice_timeout_ms(10000) {}
};

/**
 * @brief Loads configuration from a file using GLib's KeyFile API.
 *
 * This function reads configuration values from an INI-style file using GLib's
 * KeyFile API. It handles server, monitoring, and RTP output parameters.
 *
 * @param config_file_path The path to the configuration file.
 * @return A struct containing the loaded configuration.
 * @throws std::runtime_error if the file cannot be loaded or parsed.
 */
[[nodiscard]] rtp_server_config load_rtp_server_config(const std::filesystem::path &config_file_path);

/**
 * @brief Creates a default configuration file with sample values.
 *
 * This function creates a new configuration file with default values for all
 * parameters. It can be used to generate a template configuration file.
 *
 * @param config_file_path The path where the configuration file should be created.
 * @throws std::runtime_error if the file cannot be created.
 */
void create_default_rtp_server_config_file(const std::filesystem::path &config_file_path);
