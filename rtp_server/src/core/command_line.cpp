// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "command_line.h"

#include <gst/gst.h>
#include <memory>

#include "logging.h"

namespace {
    [[nodiscard]] bool parse_boolean_option_value(const char *option_name, const char *raw_value) {
        if (g_ascii_strcasecmp(raw_value, "true") == 0) {
            return true;
        }

        if (g_ascii_strcasecmp(raw_value, "false") == 0) {
            return false;
        }

        sf::error(nullptr, "Option %s must be 'true' or 'false', got: %s", option_name, raw_value);
        exit(1);
    }
} // namespace

/**
 * @brief Parses command-line arguments to get all configuration parameters.
 *
 * This implementation keeps CLI behavior unchanged while using std::optional
 * fields in command_line_parameters to indicate whether an override was
 * explicitly provided.
 *
 * @param argc The argument count from main().
 * @param argv The argument vector from main().
 * @return Parsed command-line parameters.
 */
[[nodiscard]] command_line_parameters parse_command_line(int argc, char **argv) {
    command_line_parameters params{};

    // Raw pointers for GLib option parsing
    gchar *config_file_raw = nullptr;
    gint handshake_port_raw = 0;
    guint32 background_color_raw = 0xFFFFFFFFU;
    gboolean enable_packet_loss_check_raw = FALSE;
    gboolean enable_jitter_check_raw = FALSE;
    gboolean enable_silence_check_raw = FALSE;
    gdouble max_packet_loss_rate_raw = -1.0; // Use -1.0 sentinel to detect if set
    gdouble max_jitter_ms_raw = -1.0; // Use -1.0 sentinel to detect if set
    guint64 max_silence_ms_raw = 0;
    gboolean enable_rtp_output_raw = FALSE;
    gchar *rtp_output_host_raw = nullptr;
    gint rtp_output_port_raw = 0;
    gint rtp_output_payload_type_raw = -1;
    gint common_width_raw = 0;
    gint common_height_raw = 0;
    gchar *output_directory_raw = nullptr;
    gboolean inference_engine_enable_raw = FALSE;
    gchar *deepstream_config_file_raw = nullptr;
    gchar *labels_raw = nullptr;
    gchar *groundtruth_file_raw = nullptr;
    gchar *semseg_groundtruth_file_raw = nullptr;
    gdouble test_mode_iou_threshold_raw = -1.0;
    gchar *test_mode_output_md_raw = nullptr;
    gchar *test_mode_output_json_raw = nullptr;
    gchar *semseg_output_md_raw = nullptr;
    gchar *semseg_output_json_raw = nullptr;
    gboolean tracker_enable_raw = FALSE;
    gchar *tracker_config_file_raw = nullptr;
    gchar *tracker_ll_lib_file_raw = nullptr;
    gboolean draw_borders_enable_raw = FALSE;
    gint border_width_raw = 0;
    gchar *communication_transport_type_raw = nullptr;
    gint webrtc_signaling_port_raw = 0;
    gboolean webrtc_signaling_use_ssl_raw = FALSE;
    gchar *webrtc_signaling_url_raw = nullptr;
    gchar *webrtc_stun_server_raw = nullptr;
    gchar *webrtc_turn_server_raw = nullptr;
    gchar *webrtc_turn_username_raw = nullptr;
    gchar *webrtc_turn_password_raw = nullptr;
    guint32 webrtc_ice_timeout_ms_raw = 0;
    gboolean draw_bitrate_raw = FALSE;
    gboolean draw_bitrate_large_raw = FALSE;
    gboolean draw_fps_raw = FALSE;
    gboolean save_frames_raw = FALSE;
    gchar *tet_enable_raw = nullptr;
    gchar *tet_tile_file_raw = nullptr;
    gint tet_qp_raw = -1;
    gboolean tet_dynamic_qp_raw = FALSE;
    gchar *decoder_element_raw = nullptr;
    gchar *decoder_enable_luma_backscale_raw = nullptr;

    const GOptionEntry entries[] = {
            {"config", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &config_file_raw,
             "Path to configuration file (default: rtp_server_config.ini)", nullptr},

            {"server-handshake-port", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &handshake_port_raw,
             "Set the handshake port (overrides config file)", nullptr},
            {"common-background-color", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &background_color_raw,
             "Set background color for empty buffers (overrides config file)", nullptr},

            {"monitoring-enable-packet-loss-check", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
             &enable_packet_loss_check_raw, "Enable packet loss monitoring (overrides config file)", nullptr},
            {"monitoring-enable-jitter-check", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &enable_jitter_check_raw,
             "Enable jitter monitoring (overrides config file)", nullptr},
            {"monitoring-enable-silence-check", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &enable_silence_check_raw,
             "Enable silence detection monitoring (overrides config file)", nullptr},
            {"monitoring-max-packet-loss-rate", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &max_packet_loss_rate_raw,
             "Set max packet loss rate (overrides config file)", nullptr},
            {"monitoring-max-jitter-ms", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &max_jitter_ms_raw,
             "Set max jitter threshold (overrides config file)", nullptr},
            {"monitoring-max-silence-ms", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT64, &max_silence_ms_raw,
             "Set max silence duration (overrides config file)", nullptr},

            {"rtp-output-enable", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &enable_rtp_output_raw,
             "Enable RTP output forwarding (overrides config file)", nullptr},
            {"rtp-output-host", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &rtp_output_host_raw,
             "Set RTP output destination host (overrides config file)", nullptr},
            {"rtp-output-port", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &rtp_output_port_raw,
             "Set RTP output destination port (overrides config file)", nullptr},
            {"rtp-output-payload-type", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &rtp_output_payload_type_raw,
             "Set RTP payload type for RTP output forwarding (0-127, default: 96)", nullptr},

            {"common-width", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &common_width_raw,
             "Set video width for inference configuration (overrides config file)", nullptr},
            {"common-height", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &common_height_raw,
             "Set video height for inference configuration (overrides config file)", nullptr},

            {"server-output-directory", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &output_directory_raw,
             "Set output directory for video files (overrides config file). If not specified, files are not saved "
             "(display only)",
             nullptr},

            {"inference-engine", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &inference_engine_enable_raw,
             "Enable DeepStream inference (overrides config file)", nullptr},
            {"inference-deepstream-config-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME,
             &deepstream_config_file_raw, "DeepStream YAML config file path (e.g., ../yolo/yolo_config_11m.yml)",
             nullptr},
            {"inference-labels", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &labels_raw,
             "Optional comma-separated labels filter (e.g., person,car)", nullptr},
            {"test-mode-groundtruth-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &groundtruth_file_raw,
             "COCO-style groundtruth JSON file path for test mode", nullptr},
            {"test-mode-semseg-groundtruth-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME,
             &semseg_groundtruth_file_raw, "Sparse semseg groundtruth JSON file path for test mode", nullptr},
            {"test-mode-iou-threshold", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &test_mode_iou_threshold_raw,
             "IoU threshold for test mode matching (0.0 - 1.0)", nullptr},
            {"test-mode-output-md", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &test_mode_output_md_raw,
             "Write final test mode summary to markdown file", nullptr},
            {"test-mode-output-json", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &test_mode_output_json_raw,
             "Write final test mode summary to json file", nullptr},
            {"test-mode-semseg-output-md", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &semseg_output_md_raw,
             "Write final semseg test mode summary to markdown file", nullptr},
            {"test-mode-semseg-output-json", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &semseg_output_json_raw,
             "Write final semseg test mode summary to json file", nullptr},

            {"tracker-enabled", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &tracker_enable_raw,
             "Enable DeepStream tracker (overrides config file)", nullptr},
            {"tracker-config-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &tracker_config_file_raw,
             "Tracker config file path (DeepStream YAML)", nullptr},
            {"tracker-ll-lib-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &tracker_ll_lib_file_raw,
             "nvtracker ll-lib-file path (default: DeepStream libnvds_nvmultiobjecttracker.so)", nullptr},

            {"borders-draw-borders", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &draw_borders_enable_raw,
             "Enable drawing borders around detected objects (overrides config file)", nullptr},
            {"borders-border-width", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &border_width_raw,
             "Set border width in pixels (1-64)", nullptr},

            {"server-transport-type", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &communication_transport_type_raw,
             "Set transport type (rtp, webrtc)", nullptr},

            {"webrtc-signaling-port", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &webrtc_signaling_port_raw,
             "Set signaling port for embedded mode (default: 8080)", nullptr},
            {"webrtc-signaling-ssl", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &webrtc_signaling_use_ssl_raw,
             "Enable SSL for signaling (WSS instead of WS)", nullptr},
            {"webrtc-signaling-url", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &webrtc_signaling_url_raw,
             "Set signaling URL for external mode", nullptr},
            {"webrtc-stun-server", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &webrtc_stun_server_raw,
             "Set STUN server URL (e.g., stun:stun.l.google.com:19302)", nullptr},
            {"webrtc-turn-server", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &webrtc_turn_server_raw,
             "Set TURN server URL", nullptr},
            {"webrtc-turn-username", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &webrtc_turn_username_raw,
             "Set TURN server username", nullptr},
            {"webrtc-turn-password", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &webrtc_turn_password_raw,
             "Set TURN server password", nullptr},
            {"webrtc-ice-timeout-ms", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &webrtc_ice_timeout_ms_raw,
             "Set ICE candidate timeout in milliseconds (default: 10000)", nullptr},

            {"debug-draw-bitrate", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &draw_bitrate_raw,
             "Enable bitrate display overlay (overrides config file)", nullptr},
            {"debug-draw-bitrate-large", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &draw_bitrate_large_raw,
             "Enable large red bitrate display overlay (2x size, red color) (overrides config file)", nullptr},
            {"debug-draw-fps", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &draw_fps_raw,
             "Enable FPS display overlay (overrides config file)", nullptr},
            {"debug-save-frames", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &save_frames_raw,
             "Save final reconstructed frames as PNG files (overrides config file)", nullptr},
            {"tet-enable", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &tet_enable_raw,
             "Enable or disable TET post-processing (true/false, overrides config file)", "true|false"},
            {"tet-tile-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &tet_tile_file_raw,
             "Path to TET tile file (.tet)", nullptr},
            {"tet-qp", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &tet_qp_raw,
             "Fallback QP value for TET (0-51, default: 27)", nullptr},
            {"tet-dynamic-qp", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &tet_dynamic_qp_raw,
             "Extract QP from stream instead of using --tet-qp fallback", nullptr},
            {"decoder-element", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &decoder_element_raw,
             "GStreamer decoder factory name (default: nvh265dec; openhevcdec when using gst-openhevc)", nullptr},
            {"decoder-enable-luma-backscale", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
             &decoder_enable_luma_backscale_raw,
             "Enable/disable EOI luma backscale after decode (true/false, default: true)", "true|false"},

            {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr}};

    GError *error = nullptr;
    const auto context = std::unique_ptr<GOptionContext, decltype(&g_option_context_free)>(
            g_option_context_new("- RTP Server - A GStreamer-based RTP streaming server"), &g_option_context_free);

    g_option_context_add_main_entries(context.get(), entries, nullptr);
    g_option_context_add_group(context.get(), gst_init_get_option_group());

    if (!g_option_context_parse(context.get(), &argc, &argv, &error)) {
        sf::error(nullptr, "Failed to parse command line arguments: %s", error->message);
        g_clear_error(&error);
        exit(1);
    }

    if (config_file_raw) {
        params.config_file = std::string(config_file_raw);
        g_free(config_file_raw);
    }

    if (handshake_port_raw > 0 && handshake_port_raw <= 65535) {
        params.handshake_port = static_cast<guint16>(handshake_port_raw);
    } else if (handshake_port_raw != 0) {
        sf::error(nullptr, "Port must be between 1 and 65535: %d", handshake_port_raw);
        exit(1);
    }

    if (background_color_raw != 0xFFFFFFFFU) {
        params.background_color = static_cast<sf::background_color>(background_color_raw);
    }

    if (enable_packet_loss_check_raw) {
        params.enable_packet_loss_check = true;
    }
    if (enable_jitter_check_raw) {
        params.enable_jitter_check = true;
    }
    if (enable_silence_check_raw) {
        params.enable_silence_check = true;
    }
    if (max_packet_loss_rate_raw >= 0.0) {
        if (max_packet_loss_rate_raw > 100.0) {
            sf::error(nullptr, "Packet loss rate must be between 0.0 and 100.0: %f", max_packet_loss_rate_raw);
            exit(1);
        }
        params.max_packet_loss_rate = max_packet_loss_rate_raw;
    }
    if (max_jitter_ms_raw >= 0.0) {
        params.max_jitter_ms = max_jitter_ms_raw;
    }
    if (max_silence_ms_raw > 0) {
        params.max_silence_ms = max_silence_ms_raw;
    }

    if (enable_rtp_output_raw) {
        params.enable_rtp_output = true;
    }
    if (rtp_output_host_raw) {
        params.rtp_output_host = std::string(rtp_output_host_raw);
        g_free(rtp_output_host_raw);
    }
    if (rtp_output_port_raw > 0 && rtp_output_port_raw <= 65535) {
        params.rtp_output_port = static_cast<guint16>(rtp_output_port_raw);
    } else if (rtp_output_port_raw != 0) {
        sf::error(nullptr, "RTP output port must be between 1 and 65535: %d", rtp_output_port_raw);
        exit(1);
    }
    if (rtp_output_payload_type_raw >= 0 && rtp_output_payload_type_raw <= 127) {
        params.rtp_output_payload_type = static_cast<guint>(rtp_output_payload_type_raw);
    } else if (rtp_output_payload_type_raw != -1) {
        sf::error(nullptr, "RTP output payload type must be between 0 and 127: %d", rtp_output_payload_type_raw);
        exit(1);
    }

    if (common_width_raw > 0) {
        params.video_width = common_width_raw;
    } else if (common_width_raw != 0) {
        sf::error(nullptr, "Video width must be positive: %d", common_width_raw);
        exit(1);
    }
    if (common_height_raw > 0) {
        params.video_height = common_height_raw;
    } else if (common_height_raw != 0) {
        sf::error(nullptr, "Video height must be positive: %d", common_height_raw);
        exit(1);
    }

    if (output_directory_raw) {
        params.output_directory = std::string(output_directory_raw);
        g_free(output_directory_raw);
    }

    if (inference_engine_enable_raw) {
        params.inference_engine = true;
    }
    if (deepstream_config_file_raw) {
        params.deepstream_config_file = std::string(deepstream_config_file_raw);
        g_free(deepstream_config_file_raw);
    }
    if (labels_raw) {
        params.labels = std::string(labels_raw);
        g_free(labels_raw);
    }
    if (groundtruth_file_raw) {
        params.groundtruth_file = std::string(groundtruth_file_raw);
        g_free(groundtruth_file_raw);
    }
    if (semseg_groundtruth_file_raw) {
        params.semseg_groundtruth_file = std::string(semseg_groundtruth_file_raw);
        g_free(semseg_groundtruth_file_raw);
    }
    if (test_mode_iou_threshold_raw >= 0.0) {
        if (test_mode_iou_threshold_raw > 1.0) {
            sf::error(nullptr, "Test mode IoU threshold must be between 0.0 and 1.0: %.3f",
                      test_mode_iou_threshold_raw);
            exit(1);
        }
        params.test_mode_iou_threshold = test_mode_iou_threshold_raw;
    }
    if (test_mode_output_md_raw) {
        params.test_mode_output_md = std::string(test_mode_output_md_raw);
        g_free(test_mode_output_md_raw);
    }
    if (test_mode_output_json_raw) {
        params.test_mode_output_json = std::string(test_mode_output_json_raw);
        g_free(test_mode_output_json_raw);
    }
    if (semseg_output_md_raw) {
        params.semseg_output_md = std::string(semseg_output_md_raw);
        g_free(semseg_output_md_raw);
    }
    if (semseg_output_json_raw) {
        params.semseg_output_json = std::string(semseg_output_json_raw);
        g_free(semseg_output_json_raw);
    }

    if (tracker_enable_raw) {
        params.tracker_enabled = true;
    }
    if (tracker_config_file_raw) {
        params.tracker_config_file = std::string(tracker_config_file_raw);
        g_free(tracker_config_file_raw);
    }
    if (tracker_ll_lib_file_raw) {
        params.tracker_ll_lib_file = std::string(tracker_ll_lib_file_raw);
        g_free(tracker_ll_lib_file_raw);
    }

    if (draw_borders_enable_raw) {
        params.draw_borders = true;
    }
    if (border_width_raw != 0) {
        if (border_width_raw < 1 || border_width_raw > 64) {
            sf::error(nullptr, "Border width must be between 1 and 64: %d", border_width_raw);
            exit(1);
        }
        params.border_width = static_cast<uint32_t>(border_width_raw);
    }

    if (communication_transport_type_raw) {
        const std::string transport_type_str(communication_transport_type_raw);
        if (transport_type_str != "rtp" && transport_type_str != "webrtc") {
            sf::error(nullptr, "Transport type must be 'rtp' or 'webrtc', got: %s", transport_type_str.c_str());
            g_free(communication_transport_type_raw);
            exit(1);
        }
        params.communication_transport_type = string_to_transport_type(transport_type_str);
        g_free(communication_transport_type_raw);
    }

    if (webrtc_signaling_port_raw > 0 && webrtc_signaling_port_raw <= 65535) {
        params.webrtc_signaling_port = static_cast<uint16_t>(webrtc_signaling_port_raw);
    } else if (webrtc_signaling_port_raw != 0) {
        sf::error(nullptr, "WebRTC signaling port must be between 1 and 65535: %d", webrtc_signaling_port_raw);
        exit(1);
    }
    if (webrtc_signaling_use_ssl_raw) {
        params.webrtc_signaling_use_ssl = true;
    }
    if (webrtc_signaling_url_raw) {
        params.webrtc_signaling_url = std::string(webrtc_signaling_url_raw);
        g_free(webrtc_signaling_url_raw);
    }
    if (webrtc_stun_server_raw) {
        params.webrtc_stun_server = std::string(webrtc_stun_server_raw);
        g_free(webrtc_stun_server_raw);
    }
    if (webrtc_turn_server_raw) {
        params.webrtc_turn_server = std::string(webrtc_turn_server_raw);
        g_free(webrtc_turn_server_raw);
    }
    if (webrtc_turn_username_raw) {
        params.webrtc_turn_username = std::string(webrtc_turn_username_raw);
        g_free(webrtc_turn_username_raw);
    }
    if (webrtc_turn_password_raw) {
        params.webrtc_turn_password = std::string(webrtc_turn_password_raw);
        g_free(webrtc_turn_password_raw);
    }
    if (webrtc_ice_timeout_ms_raw > 0) {
        params.webrtc_ice_timeout_ms = webrtc_ice_timeout_ms_raw;
    }

    if (draw_bitrate_raw) {
        params.draw_bitrate = true;
    }
    if (draw_bitrate_large_raw) {
        params.draw_bitrate_large = true;
    }
    if (draw_fps_raw) {
        params.draw_fps = true;
    }
    if (save_frames_raw) {
        params.save_frames = true;
    }

    if (tet_enable_raw) {
        params.tet_enabled = parse_boolean_option_value("--tet-enable", tet_enable_raw);
        g_free(tet_enable_raw);
    }
    if (tet_tile_file_raw) {
        params.tet_tile_file = std::string(tet_tile_file_raw);
        g_free(tet_tile_file_raw);
    }
    if (tet_qp_raw >= 0) {
        if (tet_qp_raw > 51) {
            sf::error(nullptr, "TET QP must be between 0 and 51: %d", tet_qp_raw);
            exit(1);
        }
        params.tet_qp = static_cast<uint32_t>(tet_qp_raw);
    }
    if (tet_dynamic_qp_raw) {
        params.tet_dynamic_qp = true;
    }
    if (decoder_element_raw != nullptr && decoder_element_raw[0] != '\0') {
        params.decoder_element = std::string(decoder_element_raw);
        g_free(decoder_element_raw);
    } else if (decoder_element_raw != nullptr) {
        g_free(decoder_element_raw);
    }

    if (decoder_enable_luma_backscale_raw) {
        params.enable_luma_backscale =
                parse_boolean_option_value("--decoder-enable-luma-backscale", decoder_enable_luma_backscale_raw);
        g_free(decoder_enable_luma_backscale_raw);
    }

    return params;
}

// Debug category for command-line parsing.
GST_DEBUG_CATEGORY_STATIC(command_line_category);
#define GST_CAT_DEFAULT command_line_category

void init_command_line_debug_category() {
    GST_DEBUG_CATEGORY_INIT(command_line_category, "command-line", 0, "Command Line Argument Parsing");
}
