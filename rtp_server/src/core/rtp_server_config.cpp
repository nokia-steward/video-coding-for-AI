// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rtp_server_config.h"

#include <climits>
#include <fstream>
#include <limits>
#include <optional>

#include "gst_raii.h"

/**
 * @file rtp_server_config.cpp
 * @brief Implementation of RTP server configuration loading and parsing.
 */

namespace {
    [[nodiscard]] std::optional<gint> read_config_integer(GKeyFile *key_file, const char *group_name,
                                                          const char *key_name, GError *&error,
                                                          const std::string &error_prefix, gint min_value,
                                                          gint max_value);

    /**
     * @brief Safely checks if a group and key exist in the key file.
     * @param key_file The GKeyFile to check.
     * @param group_name The group name to check.
     * @param key_name The key name to check.
     * @return True if both group and key exist, false otherwise.
     */
    [[nodiscard]] bool has_group_and_key(GKeyFile *key_file, const char *group_name, const char *key_name) {
        if (!g_key_file_has_group(key_file, group_name)) {
            return false;
        }
        return g_key_file_has_key(key_file, group_name, key_name, nullptr);
    }

    /**
     * @brief Loads common parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_common_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Common")) {
            return;
        }

        if (has_group_and_key(key_file, "Common", "background_color")) {
            const gint64 bg_color = g_key_file_get_int64(key_file, "Common", "background_color", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read background_color: ") + error_msg);
            }
            // Convert uint32_t to background_color enum (green = 0 in YUV)
            config.background_color = static_cast<sf::background_color>(static_cast<uint32_t>(bg_color));
        }

        const auto width_opt =
                read_config_integer(key_file, "Common", "width", error, "Failed to read Common ", 1, INT_MAX);
        if (width_opt) {
            config.video_width = width_opt.value();
        }

        const auto height_opt =
                read_config_integer(key_file, "Common", "height", error, "Failed to read Common ", 1, INT_MAX);
        if (height_opt) {
            config.video_height = height_opt.value();
        }
    }

    /**
     * @brief Loads server parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_server_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Server")) {
            return;
        }

        if (has_group_and_key(key_file, "Server", "handshake_port")) {
            const gint port = g_key_file_get_integer(key_file, "Server", "handshake_port", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read handshake_port: ") + error_msg);
            }
            if (port < 1 || port > 65535) {
                throw std::runtime_error("handshake_port must be between 1 and 65535");
            }
            config.handshake_port = static_cast<guint16>(port);
        }

        if (has_group_and_key(key_file, "Server", "transport_type")) {
            const sf::unique_g_char_ptr transport_type_str(
                    g_key_file_get_string(key_file, "Server", "transport_type", &error));
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read transport_type: ") + error_msg);
            }
            const std::string transport_type_string(transport_type_str.get());
            if (transport_type_string != "rtp" && transport_type_string != "webrtc") {
                throw std::runtime_error(std::string("Invalid transport_type: ") + transport_type_string +
                                         " (must be 'rtp' or 'webrtc')");
            }
            config.communication_transport_type = string_to_transport_type(transport_type_string);
        }
    }

    /**
     * @brief Reads a string value from configuration file with error handling.
     * @param key_file The GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Pointer to GError for error handling.
     * @param error_prefix Prefix for error messages.
     * @return String value or empty string if key doesn't exist.
     */
    [[nodiscard]] std::string read_config_string(GKeyFile *key_file, const char *group_name, const char *key_name,
                                                 GError *&error, const std::string &error_prefix) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return "";
        }

        const sf::unique_g_char_ptr value(g_key_file_get_string(key_file, group_name, key_name, &error));
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + key_name + ": " + error_msg);
        }
        return std::string(value.get());
    }

    /**
     * @brief Reads an integer value from configuration file with validation.
     * @param key_file The GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Pointer to GError for error handling.
     * @param error_prefix Prefix for error messages.
     * @param min_value Minimum allowed value.
     * @param max_value Maximum allowed value.
     * @return Integer value or std::nullopt if key doesn't exist.
     */
    [[nodiscard]] std::optional<gint> read_config_integer(GKeyFile *key_file, const char *group_name,
                                                          const char *key_name, GError *&error,
                                                          const std::string &error_prefix, gint min_value = 0,
                                                          gint max_value = INT_MAX) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return std::nullopt;
        }

        const gint value = g_key_file_get_integer(key_file, group_name, key_name, &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + key_name + ": " + error_msg);
        }
        if (value < min_value || value > max_value) {
            throw std::runtime_error(error_prefix + key_name + " must be between " + std::to_string(min_value) +
                                     " and " + std::to_string(max_value));
        }
        return value;
    }

    /**
     * @brief Reads a boolean value from configuration file with error handling.
     * @param key_file The GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Pointer to GError for error handling.
     * @param error_prefix Prefix for error messages.
     * @return Boolean value or std::nullopt if key doesn't exist.
     */
    [[nodiscard]] std::optional<bool> read_config_boolean(GKeyFile *key_file, const char *group_name,
                                                          const char *key_name, GError *&error,
                                                          const std::string &error_prefix) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return std::nullopt;
        }

        const gboolean value = g_key_file_get_boolean(key_file, group_name, key_name, &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + key_name + ": " + error_msg);
        }
        return value != FALSE;
    }

    /**
     * @brief Loads WebRTC signaling mode parameter.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_webrtc_signaling_mode(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        const std::string signaling_mode_str =
                read_config_string(key_file, "WebRTC", "signaling_mode", error, "Failed to read WebRTC ");
        if (signaling_mode_str.empty()) {
            return;
        }

        if (signaling_mode_str != "embedded" && signaling_mode_str != "external") {
            throw std::runtime_error("Invalid WebRTC signaling_mode: " + signaling_mode_str +
                                     " (must be 'embedded' or 'external')");
        }
        config.webrtc_signaling_mode_enum = string_to_webrtc_signaling_mode(signaling_mode_str);
    }

    /**
     * @brief Loads WebRTC string parameters.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_webrtc_string_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        const std::string signaling_url =
                read_config_string(key_file, "WebRTC", "signaling_url", error, "Failed to read WebRTC ");
        if (!signaling_url.empty()) {
            config.webrtc_signaling_url = signaling_url;
        }

        const std::string stun_server =
                read_config_string(key_file, "WebRTC", "stun_server", error, "Failed to read WebRTC ");
        if (!stun_server.empty()) {
            config.webrtc_stun_server = stun_server;
        }

        const std::string turn_server =
                read_config_string(key_file, "WebRTC", "turn_server", error, "Failed to read WebRTC ");
        if (!turn_server.empty()) {
            config.webrtc_turn_server = turn_server;
        }

        const std::string turn_username =
                read_config_string(key_file, "WebRTC", "turn_username", error, "Failed to read WebRTC ");
        if (!turn_username.empty()) {
            config.webrtc_turn_username = turn_username;
        }

        const std::string turn_password =
                read_config_string(key_file, "WebRTC", "turn_password", error, "Failed to read WebRTC ");
        if (!turn_password.empty()) {
            config.webrtc_turn_password = turn_password;
        }
    }

    /**
     * @brief Loads WebRTC parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_webrtc_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "WebRTC")) {
            return;
        }

        load_webrtc_signaling_mode(key_file, config, error);

        const auto signaling_port_opt =
                read_config_integer(key_file, "WebRTC", "signaling_port", error, "Failed to read WebRTC ", 1, 65535);
        if (signaling_port_opt) {
            config.webrtc_signaling_port = static_cast<uint16_t>(signaling_port_opt.value());
        }

        const auto signaling_use_ssl_opt =
                read_config_boolean(key_file, "WebRTC", "signaling_use_ssl", error, "Failed to read WebRTC ");
        if (signaling_use_ssl_opt) {
            config.webrtc_signaling_use_ssl = signaling_use_ssl_opt.value();
        }

        load_webrtc_string_parameters(key_file, config, error);

        const auto ice_timeout_opt =
                read_config_integer(key_file, "WebRTC", "ice_timeout_ms", error, "Failed to read WebRTC ");
        if (ice_timeout_opt) {
            config.webrtc_ice_timeout_ms = static_cast<uint32_t>(ice_timeout_opt.value());
        }
    }

    /**
     * @brief Reads a double value from configuration file with validation.
     * @param key_file The GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Pointer to GError for error handling.
     * @param error_prefix Prefix for error messages.
     * @param min_value Minimum allowed value.
     * @param max_value Maximum allowed value.
     * @return Double value or std::nullopt if key doesn't exist.
     */
    [[nodiscard]] std::optional<gdouble> read_config_double(GKeyFile *key_file, const char *group_name,
                                                            const char *key_name, GError *&error,
                                                            const std::string &error_prefix, gdouble min_value = 0.0,
                                                            gdouble max_value = std::numeric_limits<gdouble>::max()) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return std::nullopt;
        }

        const gdouble value = g_key_file_get_double(key_file, group_name, key_name, &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + key_name + ": " + error_msg);
        }
        if (value < min_value || value > max_value) {
            throw std::runtime_error(error_prefix + key_name + " must be between " + std::to_string(min_value) +
                                     " and " + std::to_string(max_value));
        }
        return value;
    }

    /**
     * @brief Reads an int64 value from configuration file with validation.
     * @param key_file The GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Pointer to GError for error handling.
     * @param error_prefix Prefix for error messages.
     * @param min_value Minimum allowed value.
     * @return Int64 value or std::nullopt if key doesn't exist.
     */
    [[nodiscard]] std::optional<gint64> read_config_int64(GKeyFile *key_file, const char *group_name,
                                                          const char *key_name, GError *&error,
                                                          const std::string &error_prefix, gint64 min_value = 1) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return std::nullopt;
        }

        const gint64 value = g_key_file_get_int64(key_file, group_name, key_name, &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + key_name + ": " + error_msg);
        }
        if (value < min_value) {
            throw std::runtime_error(error_prefix + key_name + " must be positive");
        }
        return value;
    }

    /**
     * @brief Loads monitoring boolean flags from configuration.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_monitoring_boolean_flags(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        const auto packet_loss_check_opt =
                read_config_boolean(key_file, "Monitoring", "enable_packet_loss_check", error, "Failed to read ");
        if (packet_loss_check_opt) {
            config.monitoring_config.enable_packet_loss_check = packet_loss_check_opt.value();
        }

        const auto jitter_check_opt =
                read_config_boolean(key_file, "Monitoring", "enable_jitter_check", error, "Failed to read ");
        if (jitter_check_opt) {
            config.monitoring_config.enable_jitter_check = jitter_check_opt.value();
        }

        const auto silence_check_opt =
                read_config_boolean(key_file, "Monitoring", "enable_silence_check", error, "Failed to read ");
        if (silence_check_opt) {
            config.monitoring_config.enable_silence_check = silence_check_opt.value();
        }
    }

    /**
     * @brief Loads monitoring threshold parameters from configuration.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_monitoring_thresholds(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        const auto packet_loss_rate_opt = read_config_double(key_file, "Monitoring", "max_packet_loss_rate", error,
                                                             "Failed to read ", 0.0, 100.0);
        if (packet_loss_rate_opt) {
            config.monitoring_config.max_packet_loss_rate = packet_loss_rate_opt.value();
        }

        const auto jitter_ms_opt =
                read_config_double(key_file, "Monitoring", "max_jitter_ms", error, "Failed to read ", 0.0);
        if (jitter_ms_opt) {
            config.monitoring_config.max_jitter_ms = jitter_ms_opt.value();
        }

        const auto silence_ms_opt =
                read_config_int64(key_file, "Monitoring", "max_silence_ms", error, "Failed to read ", 1);
        if (silence_ms_opt) {
            config.monitoring_config.max_silence_ms = static_cast<uint64_t>(silence_ms_opt.value());
        }
    }

    /**
     * @brief Loads monitoring parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_monitoring_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Monitoring")) {
            return;
        }

        load_monitoring_boolean_flags(key_file, config, error);
        load_monitoring_thresholds(key_file, config, error);
    }

    /**
     * @brief Loads RTP output parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_rtp_output_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "RTPOutput")) {
            return;
        }

        if (const auto enabled =
                    read_config_boolean(key_file, "RTPOutput", "enabled", error, "Failed to read RTPOutput ")) {
            config.rtp_output.enabled = enabled.value();
        }

        const std::string host = read_config_string(key_file, "RTPOutput", "host", error, "Failed to read RTPOutput ");
        if (!host.empty()) {
            config.rtp_output.host = host;
        }

        if (const auto port =
                    read_config_integer(key_file, "RTPOutput", "port", error, "Failed to read RTPOutput ", 1, 65535)) {
            config.rtp_output.port = static_cast<guint16>(port.value());
        }

        if (const auto payload_type = read_config_integer(key_file, "RTPOutput", "payload_type", error,
                                                          "Failed to read RTPOutput ", 0, 127)) {
            config.rtp_output.payload_type = static_cast<guint>(payload_type.value());
        }
    }

    /**
     * @brief Loads output directory parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_output_directory_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Server")) {
            return;
        }

        const std::string output_dir =
                read_config_string(key_file, "Server", "output_directory", error, "Failed to read ");
        if (!output_dir.empty()) {
            config.output_directory = output_dir;
        }
    }

    /**
     * @brief Loads debug parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_debug_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Debug")) {
            return;
        }

        if (const auto enable_display_sink =
                    read_config_boolean(key_file, "Debug", "enable_display_sink", error, "Failed to read ")) {
            config.enable_display_sink = enable_display_sink.value();
        }
        if (const auto draw_bitrate =
                    read_config_boolean(key_file, "Debug", "draw_bitrate", error, "Failed to read ")) {
            config.draw_bitrate = draw_bitrate.value();
        }
        if (const auto draw_bitrate_large =
                    read_config_boolean(key_file, "Debug", "draw_bitrate_large", error, "Failed to read ")) {
            config.draw_bitrate_large = draw_bitrate_large.value();
        }
        if (const auto draw_fps = read_config_boolean(key_file, "Debug", "draw_fps", error, "Failed to read ")) {
            config.draw_fps = draw_fps.value();
        }
        if (const auto save_frames = read_config_boolean(key_file, "Debug", "save_frames", error, "Failed to read ")) {
            config.save_frames = save_frames.value();
        }
    }

    /**
     * @brief Loads TET parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_tet_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "TET")) {
            return;
        }

        const auto enabled_opt = read_config_boolean(key_file, "TET", "enabled", error, "Failed to read TET ");
        if (enabled_opt) {
            config.tet_enabled = enabled_opt.value();
        }

        const std::string tile_file = read_config_string(key_file, "TET", "tile_file", error, "Failed to read TET ");
        if (!tile_file.empty()) {
            config.tet_tile_file = tile_file;
        }

        const auto qp_opt = read_config_integer(key_file, "TET", "qp", error, "Failed to read TET ", 0, 51);
        if (qp_opt) {
            config.tet_qp = static_cast<uint32_t>(qp_opt.value());
        }

        const auto dynamic_qp_opt = read_config_boolean(key_file, "TET", "dynamic_qp", error, "Failed to read TET ");
        if (dynamic_qp_opt) {
            config.tet_dynamic_qp = dynamic_qp_opt.value();
        }
    }

    /**
     * @brief Loads decoder factory parameters from the configuration file.
     */
    void load_decoder_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Decoder")) {
            return;
        }
        const std::string element_str =
                read_config_string(key_file, "Decoder", "element", error, "Failed to read Decoder ");
        if (!element_str.empty()) {
            config.decoder_element = element_str;
        }
        if (const auto enable_backscale = read_config_boolean(key_file, "Decoder", "enable_luma_backscale", error,
                                                              "Failed to read Decoder ")) {
            config.enable_luma_backscale = enable_backscale.value();
        }
    }

    /**
     * @brief Loads inference parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_inference_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Inference")) {
            return;
        }

        const auto inference_engine_opt =
                read_config_boolean(key_file, "Inference", "inference_engine", error, "Failed to read Inference ");
        if (inference_engine_opt) {
            config.inference_engine = inference_engine_opt.value();
        }

        const std::string deepstream_config_file =
                read_config_string(key_file, "Inference", "deepstream_config_file", error, "Failed to read Inference ");
        if (!deepstream_config_file.empty()) {
            config.deepstream_config_file = deepstream_config_file;
        }

        const std::string semseg_deepstream_config_file = read_config_string(
                key_file, "Inference", "semseg_deepstream_config_file", error, "Failed to read Inference ");
        if (!semseg_deepstream_config_file.empty()) {
            config.semseg_deepstream_config_file = semseg_deepstream_config_file;
        }

        const std::string semseg_model_file =
                read_config_string(key_file, "Inference", "semseg_model_file", error, "Failed to read Inference ");
        if (!semseg_model_file.empty()) {
            config.semseg_model_file = semseg_model_file;
        }

        const std::string semseg_engine_file =
                read_config_string(key_file, "Inference", "semseg_engine_file", error, "Failed to read Inference ");
        if (!semseg_engine_file.empty()) {
            config.semseg_engine_file = semseg_engine_file;
        }

        const std::string semseg_labels_file =
                read_config_string(key_file, "Inference", "semseg_labels_file", error, "Failed to read Inference ");
        if (!semseg_labels_file.empty()) {
            config.semseg_labels_file = semseg_labels_file;
        }

        const std::string semseg_profile =
                read_config_string(key_file, "Inference", "semseg_profile", error, "Failed to read Inference ");
        if (!semseg_profile.empty()) {
            config.semseg_profile = semseg_profile;
        }

        const std::string labels =
                read_config_string(key_file, "Inference", "labels", error, "Failed to read Inference ");
        if (!labels.empty()) {
            config.labels = labels;
        }
    }

    /**
     * @brief Loads test mode parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_test_mode_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "TestMode")) {
            return;
        }

        const std::string groundtruth_file =
                read_config_string(key_file, "TestMode", "groundtruth_file", error, "Failed to read TestMode ");
        if (!groundtruth_file.empty()) {
            config.groundtruth_file = groundtruth_file;
        }

        const std::string semseg_groundtruth_file =
                read_config_string(key_file, "TestMode", "semseg_groundtruth_file", error, "Failed to read TestMode ");
        if (!semseg_groundtruth_file.empty()) {
            config.semseg_groundtruth_file = semseg_groundtruth_file;
        }

        const auto iou_threshold_opt =
                read_config_double(key_file, "TestMode", "iou_threshold", error, "Failed to read TestMode ", 0.0, 1.0);
        if (iou_threshold_opt) {
            config.test_mode_iou_threshold = iou_threshold_opt.value();
        }

        const std::string output_md =
                read_config_string(key_file, "TestMode", "output_md", error, "Failed to read TestMode ");
        if (!output_md.empty()) {
            config.test_mode_output_md = output_md;
        }

        const std::string output_json =
                read_config_string(key_file, "TestMode", "output_json", error, "Failed to read TestMode ");
        if (!output_json.empty()) {
            config.test_mode_output_json = output_json;
        }

        const std::string semseg_output_md =
                read_config_string(key_file, "TestMode", "semseg_output_md", error, "Failed to read TestMode ");
        if (!semseg_output_md.empty()) {
            config.semseg_output_md = semseg_output_md;
        }

        const std::string semseg_output_json =
                read_config_string(key_file, "TestMode", "semseg_output_json", error, "Failed to read TestMode ");
        if (!semseg_output_json.empty()) {
            config.semseg_output_json = semseg_output_json;
        }
    }

    /**
     * @brief Loads tracker parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_tracker_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Tracker")) {
            return;
        }

        const auto tracker_enabled_opt =
                read_config_boolean(key_file, "Tracker", "tracker_enabled", error, "Failed to read Tracker ");
        if (tracker_enabled_opt) {
            config.tracker_enabled = tracker_enabled_opt.value();
        }

        const std::string tracker_config_file =
                read_config_string(key_file, "Tracker", "tracker_config_file", error, "Failed to read Tracker ");
        if (!tracker_config_file.empty()) {
            config.tracker_config_file = tracker_config_file;
        }

        const std::string tracker_ll_lib_file =
                read_config_string(key_file, "Tracker", "tracker_ll_lib_file", error, "Failed to read Tracker ");
        if (!tracker_ll_lib_file.empty()) {
            config.tracker_ll_lib_file = tracker_ll_lib_file;
        }
    }

    /**
     * @brief Loads border parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The rtp_server_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_border_parameters(GKeyFile *key_file, rtp_server_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Borders")) {
            return;
        }

        const auto draw_borders_opt =
                read_config_boolean(key_file, "Borders", "draw_borders", error, "Failed to read Borders ");
        if (draw_borders_opt) {
            config.draw_borders = draw_borders_opt.value();
        }

        const auto border_width_opt =
                read_config_integer(key_file, "Borders", "border_width", error, "Failed to read Borders ", 1, 64);
        if (border_width_opt) {
            config.border_width = static_cast<uint32_t>(border_width_opt.value());
        }
    }
} // namespace

rtp_server_config load_rtp_server_config(const std::filesystem::path &config_file_path) {
    rtp_server_config config{};

    const sf::unique_g_key_file_ptr key_file(g_key_file_new());
    GError *error = nullptr;

    // Load the configuration file
    if (!g_key_file_load_from_file(key_file.get(), config_file_path.c_str(), G_KEY_FILE_NONE, &error)) {
        const std::string error_msg = error ? error->message : "Unknown error";
        g_clear_error(&error);
        throw std::runtime_error(std::string("Failed to load configuration file: ") + error_msg);
    }

    // Load all configuration sections
    load_common_parameters(key_file.get(), config, error);
    load_server_parameters(key_file.get(), config, error);
    load_monitoring_parameters(key_file.get(), config, error);
    load_rtp_output_parameters(key_file.get(), config, error);
    load_output_directory_parameters(key_file.get(), config, error);
    load_inference_parameters(key_file.get(), config, error);
    load_test_mode_parameters(key_file.get(), config, error);
    load_tracker_parameters(key_file.get(), config, error);
    load_border_parameters(key_file.get(), config, error);
    load_debug_parameters(key_file.get(), config, error);
    load_tet_parameters(key_file.get(), config, error);
    load_decoder_parameters(key_file.get(), config, error);
    load_webrtc_parameters(key_file.get(), config, error);

    return config;
}

void create_default_rtp_server_config_file(const std::filesystem::path &config_file_path) {
    std::ofstream file(config_file_path);
    if (!file.is_open()) {
        throw std::runtime_error(std::string("Failed to create configuration file: ") + config_file_path.string());
    }

    file << "# RTP Server Configuration File\n";
    file << "# This file contains all configuration parameters for the RTP server application\n";
    file << "#\n";
    file << "# OVERVIEW:\n";
    file << "# The RTP Server is a GStreamer-based RTP streaming server that:\n";
    file << "# 1. Listens for client connections on a handshake port\n";
    file << "# 2. Dynamically allocates RTP ports for video streaming\n";
    file << "# 3. Receives H.265 encoded video streams via RTP\n";
    file << "# 4. Decodes and displays video streams\n";
    file << "# 5. Saves decoded streams to H.265 files\n";
    file << "# 6. Optionally forwards streams to RTP output destinations\n";
    file << "# 7. Monitors stream quality (packet loss, jitter, silence)\n";
    file << "\n";
    file << "[Server]\n";
    file << "# SERVER PARAMETERS\n";
    file << "# These parameters control the basic server operation and client connection handling.\n";
    file << "\n";
    file << "# Handshake port for client connections\n";
    file << "# This is the RTP input handshake port where the server listens for new client connection requests\n";
    file << "# When a client connects, the server dynamically allocates a new RTP port for video streaming\n";
    file << "#\n";
    file << "# Valid range: 1-65535\n";
    file << "# Common ports: 5000-5010 (standard RTP range)\n";
    file << "# Default: 5000\n";
    file << "#\n";
    file << "# Example configurations:\n";
    file << "#   5000  - Default handshake port\n";
    file << "#   5004  - Alternative port (if 5000 is in use)\n";
    file << "#   6000  - Custom port\n";
    file << "handshake_port = 5000\n";
    file << "\n";
    file << "# Transport type\n";
    file << "# Options: rtp, webrtc\n";
    file << "# Default: rtp\n";
    file << "#\n";
    file << "#   rtp    - RTP input transport with binary handshake protocol (default)\n";
    file << "#   webrtc - WebRTC transport with signaling server\n";
    file << "transport_type = rtp\n";
    file << "\n";
    file << "# Output directory for saving video files\n";
    file << "# When specified, video streams will be saved to H.265 files in this directory\n";
    file << "# If not specified or empty, files are not saved (display only)\n";
    file << "#\n";
    file << "# Examples:\n";
    file << "#   /tmp/rtp_recordings  - Save to /tmp/rtp_recordings directory\n";
    file << "#   ./recordings         - Save to ./recordings directory relative to current directory\n";
    file << "#   (empty or not set)   - No file saving (display only)\n";
    file << "#\n";
    file << "# Note: The directory will be created automatically if it doesn't exist\n";
    file << "#       File names are auto-generated with timestamp and port number\n";
    file << "#       Format: rtp_stream_port<PORT>_<YYYYMMDD_HHMMSS>_<ms>.h265\n";
    file << "#output_directory = \n";
    file << "\n";
    file << "[Monitoring]\n";
    file << "# MONITORING PARAMETERS\n";
    file << "# These parameters control RTP stream quality monitoring and quality checks.\n";
    file << "# Monitoring helps detect and report issues with incoming video streams.\n";
    file << "\n";
    file << "# Enable packet loss monitoring\n";
    file << "# When enabled, the server monitors packet loss rates in incoming RTP streams\n";
    file << "# and reports warnings if the loss rate exceeds the threshold\n";
    file << "#\n";
    file << "# Options:\n";
    file << "#   true  - Enable packet loss monitoring\n";
    file << "#   false - Disable packet loss monitoring (default)\n";
    file << "enable_packet_loss_check = false\n";
    file << "\n";
    file << "# Enable jitter monitoring\n";
    file << "# When enabled, the server monitors network jitter (variation in packet arrival times)\n";
    file << "# and reports warnings if jitter exceeds the threshold\n";
    file << "#\n";
    file << "# Options:\n";
    file << "#   true  - Enable jitter monitoring\n";
    file << "#   false - Disable jitter monitoring (default)\n";
    file << "enable_jitter_check = false\n";
    file << "\n";
    file << "# Enable silence detection monitoring\n";
    file << "# When enabled, the server monitors for periods of silence (no data received)\n";
    file << "# and reports warnings if silence duration exceeds the threshold\n";
    file << "#\n";
    file << "# Options:\n";
    file << "#   true  - Enable silence detection monitoring\n";
    file << "#   false - Disable silence detection monitoring (default)\n";
    file << "enable_silence_check = false\n";
    file << "\n";
    file << "# Maximum packet loss rate threshold (percentage)\n";
    file << "# This threshold is only used when enable_packet_loss_check = true\n";
    file << "# The server will report warnings if packet loss rate exceeds this value\n";
    file << "#\n";
    file << "# Range: 0.0 to 100.0 (percentage)\n";
    file << "# Default: 5.0\n";
    file << "max_packet_loss_rate = 5.0\n";
    file << "\n";
    file << "# Maximum jitter threshold (milliseconds)\n";
    file << "# This threshold is only used when enable_jitter_check = true\n";
    file << "# The server will report warnings if jitter exceeds this value\n";
    file << "#\n";
    file << "# Range: 0.0 and above (milliseconds)\n";
    file << "# Default: 50.0\n";
    file << "max_jitter_ms = 50.0\n";
    file << "\n";
    file << "# Maximum silence duration threshold (milliseconds)\n";
    file << "# This threshold is only used when enable_silence_check = true\n";
    file << "# The server will report warnings if silence duration exceeds this value\n";
    file << "#\n";
    file << "# Range: 1 and above (milliseconds)\n";
    file << "# Default: 10000 (10 seconds)\n";
    file << "max_silence_ms = 10000\n";
    file << "\n";
    file << "[RTPOutput]\n";
    file << "# RTP OUTPUT PARAMETERS\n";
    file << "# These parameters configure RTP output forwarding for H.265 RTP video streams.\n";
    file << "# RTP output allows the server to forward video streams to other applications or systems.\n";
    file << "\n";
    file << "# Enable RTP output forwarding\n";
    file << "# When enabled, the server forwards H.265 RTP video streams to a specified destination\n";
    file << "# This creates an additional output branch in the pipeline that encodes, RTP-payloads,\n";
    file << "# and sends video through RTP output transport\n";
    file << "#\n";
    file << "# Options:\n";
    file << "#   true  - Enable RTP output forwarding\n";
    file << "#   false - Disable RTP output forwarding (default)\n";
    file << "enabled = false\n";
    file << "\n";
    file << "# RTP output destination host address\n";
    file << "# This is the IP address or hostname where RTP output packets will be sent\n";
    file << "# This parameter is only used when enabled = true\n";
    file << "#\n";
    file << "# Common formats:\n";
    file << "#   IP address: 192.168.1.100, 10.0.0.5\n";
    file << "#   Hostname:   stream-forward.local, rtp-output-server.company.com\n";
    file << "#   Localhost:  127.0.0.1, localhost\n";
    file << "#\n";
    file << "# Default: 127.0.0.1 (localhost)\n";
    file << "host = 127.0.0.1\n";
    file << "\n";
    file << "# RTP output destination port number\n";
    file << "# This is the RTP output destination port number where packets will be sent\n";
    file << "# This parameter is only used when enabled = true\n";
    file << "#\n";
    file << "# Valid range: 1-65535\n";
    file << "# Common RTP output ports: 5004-5010 (standard RTP range)\n";
    file << "# Default: 5004\n";
    file << "port = 5004\n";
    file << "\n";
    file << "# RTP payload type for forwarded H.265 stream\n";
    file << "# Valid range: 0-127 (dynamic payloads commonly use 96)\n";
    file << "# Default: 96\n";
    file << "payload_type = 96\n";
    file << "\n";
    file << "[Common]\n";
    file << "# COMMON PARAMETERS\n";
    file << "# These parameters are shared across different components.\n";
    file << "\n";
    file << "# Background color for empty buffers\n";
    file << "# This color is used when initializing empty video buffers\n";
    file << "# Value is a 32-bit integer representing the color\n";
    file << "# Default: 0\n";
    file << "background_color = 0\n";
    file << "\n";
    file << "# Video width/height for inference configuration\n";
    file << "# These values are used to select and configure YOLO models at startup.\n";
    file << "# Leave empty to use defaults or model-specific config values.\n";
    file << "#width = 2560\n";
    file << "#height = 1600\n";
    file << "\n";
    file << "[Debug]\n";
    file << "# DEBUG PARAMETERS\n";
    file << "# These parameters control debugging and diagnostic features.\n";
    file << "\n";
    file << "# Enable on-screen display sink output\n";
    file << "# When disabled, a fakesink is used instead of nveglglessink so the pipeline runs headless.\n";
    file << "#\n";
    file << "# Options:\n";
    file << "#   true  - Render to on-screen display sink (default)\n";
    file << "#   false - Disable display rendering (headless sink)\n";
    file << "#\n";
    file << "enable_display_sink = true\n";
    file << "\n";
    file << "# Enable bitrate display overlay\n";
    file << "# When enabled, the server calculates and displays the current bitrate as an overlay\n";
    file << "# on decoded video frames. The bitrate is calculated using a sliding window approach\n";
    file << "# and displayed in the lower right corner of the video.\n";
    file << "#\n";
    file << "# Options:\n";
    file << "#   true  - Enable bitrate display overlay\n";
    file << "#   false - Disable bitrate display overlay (default)\n";
    file << "#\n";
    file << "# Use cases:\n";
    file << "#   true  - Debugging bitrate issues, monitoring stream quality, development\n";
    file << "#   false - Production environments, when overlay is not needed\n";
    file << "draw_bitrate = false\n";
    file << "\n";
    file << "# Enable large red bitrate display overlay\n";
    file << "# When enabled, the server calculates and displays the current bitrate as an overlay\n";
    file << "# on decoded video frames. The bitrate is displayed at 2x the normal size and in red color\n";
    file << "# in the lower right corner of the video.\n";
    file << "#\n";
    file << "# Options:\n";
    file << "#   true  - Enable large red bitrate display overlay\n";
    file << "#   false - Disable large red bitrate display overlay (default)\n";
    file << "#\n";
    file << "# Use cases:\n";
    file << "#   true  - When larger, more visible bitrate display is needed\n";
    file << "#   false - Use normal bitrate overlay or disable overlay\n";
    file << "draw_bitrate_large = false\n";
    file << "\n";
    file << "# Enable FPS display overlay\n";
    file << "# When enabled, the server calculates and displays the current playback FPS as an overlay\n";
    file << "# on decoded frames. The FPS value is displayed in the top right corner of the video.\n";
    file << "#\n";
    file << "#   true  - Enable FPS display overlay\n";
    file << "#   false - Disable FPS display overlay (default)\n";
    file << "#\n";
    file << "draw_fps = false\n";
    file << "\n";
    file << "# Save final reconstructed frames as PNG files for debugging\n";
    file << "# When enabled, frames are saved after PRI reconstruction and optional TET processing.\n";
    file << "# Output path:\n";
    file << "#   - If [Server] output_directory is set: <output_directory>/debug_frames/port_<PORT>/\n";
    file << "#   - Otherwise: <config_file_directory>/debug_frames/port_<PORT>/\n";
    file << "# File name pattern: frame_000001.png\n";
    file << "save_frames = false\n";
    file << "\n";

    file << "[TET]\n";
    file << "# TET PARAMETERS\n";
    file << "# These parameters control Tile Enhancement Tiles (TET) post-processing.\n";
    file << "# When enabled, decoded frames are modified by adding learned tile patterns.\n";
    file << "\n";
    file << "# Enable TET post-processing\n";
    file << "# Options: true, false\n";
    file << "enabled = false\n";
    file << "\n";
    file << "# Path to the TET tile file (.tet)\n";
    file << "# Example: tiles/yuv_32x32_checkerboard.tet\n";
    file << "tile_file = \n";
    file << "\n";
    file << "# Fallback QP value for selecting tiles\n";
    file << "# Valid range: 0-51\n";
    file << "qp = 27\n";
    file << "\n";
    file << "# Enable dynamic QP extraction from encoded stream\n";
    file << "# When enabled, SPS/PPS QP values are used for tile selection\n";
    file << "# Options: true, false\n";
    file << "dynamic_qp = false\n";
    file << "\n";

    file << "[Decoder]\n";
    file << "# Optional decoder factory name; omit to use default nvh265dec.\n";
    file << "# openhevcdec = software decoder (gst-openhevc); ensure plugin path is discoverable via "
            "GST_PLUGIN_PATH.\n";
    file << "# element = nvh265dec\n";
    file << "# element = openhevcdec\n";
    file << "\n";
    file << "# Apply the EOI luma backscale (inverse of encode-side luma adaptation) after decode.\n";
    file << "# Options: true, false\n";
    file << "enable_luma_backscale = true\n";
    file << "\n";

    file << "[Inference]\n";
    file << "# INFERENCE PARAMETERS\n";
    file << "# These parameters control DeepStream inference in the display branch.\n";
    file << "# When inference is disabled, decoded frames are displayed without DeepStream processing.\n";
    file << "\n";
    file << "# Enable inference engine (nvinfer) for detection\n";
    file << "# Options: true, false\n";
    file << "inference_engine = false\n";
    file << "\n";
    file << "# DeepStream YAML config for inference (e.g. ../yolo/yolo_config_11m.yml)\n";
    file << "# Leave empty to use no inference configuration.\n";
    file << "deepstream_config_file = \n";
    file << "\n";
    file << "# Optional DeepStream YAML config dedicated to semantic segmentation\n";
    file << "# Used by scripts/run_tet_comparison.sh semseg auto-resolution.\n";
    file << "semseg_deepstream_config_file = \n";
    file << "\n";
    file << "# Optional semantic-segmentation ONNX model path used for auto-generation\n";
    file << "semseg_model_file = \n";
    file << "\n";
    file << "# Optional semantic-segmentation TensorRT engine path used for auto-generation\n";
    file << "semseg_engine_file = \n";
    file << "\n";
    file << "# Optional semantic-segmentation labels file path used for auto-generation\n";
    file << "semseg_labels_file = \n";
    file << "\n";
    file << "# Optional semantic-segmentation profile name used for generated config files\n";
    file << "semseg_profile = default\n";
    file << "\n";
    file << "# Optional comma-separated labels filter (e.g. person,car)\n";
    file << "# Leave empty to draw borders for all detected classes.\n";
    file << "labels = \n";
    file << "\n";

    file << "[TestMode]\n";
    file << "# TEST MODE PARAMETERS\n";
    file << "# These parameters enable detection accuracy testing against groundtruth.\n";
    file << "# When groundtruth_file is set, detections are compared right after inference.\n";
    file << "\n";
    file << "# Path to COCO-style groundtruth JSON file (e.g. "
            "../Traffic_2560x1600_30_val/annotations/Traffic_2560x1600_30_val.json)\n";
    file << "# Leave empty to disable test mode.\n";
    file << "groundtruth_file = \n";
    file << "\n";
    file << "# Path to sparse semantic-segmentation groundtruth JSON file\n";
    file << "# Leave empty to disable semantic-segmentation test mode.\n";
    file << "semseg_groundtruth_file = \n";
    file << "\n";
    file << "# IoU threshold for matching detections to groundtruth\n";
    file << "# Valid range: 0.0 - 1.0\n";
    file << "iou_threshold = 0.5\n";
    file << "\n";
    file << "# Optional markdown output file for final test mode summary\n";
    file << "# Example: /tmp/test_mode_summary.md\n";
    file << "output_md = \n";
    file << "\n";
    file << "# Optional json output file for final test mode summary\n";
    file << "# Example: /tmp/test_mode_summary.json\n";
    file << "output_json = \n";
    file << "\n";
    file << "# Optional markdown output file for final semseg test mode summary\n";
    file << "# Example: /tmp/test_mode_semseg_summary.md\n";
    file << "semseg_output_md = \n";
    file << "\n";
    file << "# Optional json output file for final semseg test mode summary\n";
    file << "# Example: /tmp/test_mode_semseg_summary.json\n";
    file << "semseg_output_json = \n";
    file << "\n";

    file << "[Tracker]\n";
    file << "# TRACKER PARAMETERS\n";
    file << "# These parameters control DeepStream tracking (nvtracker) in the display branch.\n";
    file << "\n";
    file << "# Enable tracker\n";
    file << "# Options: true, false\n";
    file << "tracker_enabled = false\n";
    file << "\n";
    file << "# Optional tracker config file path (DeepStream YAML)\n";
    file << "tracker_config_file = \n";
    file << "\n";
    file << "# Low-level tracker library file used by nvtracker\n";
    file << "tracker_ll_lib_file = /opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so\n";
    file << "\n";

    file << "[Borders]\n";
    file << "# BORDER PARAMETERS\n";
    file << "# These parameters control border drawing on decoded display frames.\n";
    file << "\n";
    file << "# Draw blue borders around detected objects (display only)\n";
    file << "# Options: true, false\n";
    file << "draw_borders = false\n";
    file << "\n";
    file << "# Border width in pixels\n";
    file << "border_width = 2\n";
    file << "\n";

    file << "[WebRTC]\n";
    file << "# WEBRTC PARAMETERS\n";
    file << "# These parameters are only used when transport_type=webrtc\n";
    file << "\n";
    file << "# Signaling mode\n";
    file << "# Options: embedded, external\n";
    file << "# Default: embedded\n";
    file << "#\n";
    file << "#   embedded - Server acts as signaling server (simpler deployment)\n";
    file << "#   external  - Connect to external signaling server\n";
    file << "signaling_mode = embedded\n";
    file << "\n";
    file << "# Signaling port (for embedded mode)\n";
    file << "# WebSocket port for signaling server\n";
    file << "# Valid range: 1-65535\n";
    file << "# Default: 8080\n";
    file << "signaling_port = 8080\n";
    file << "\n";
    file << "# Use SSL for signaling (for embedded mode)\n";
    file << "# Enable WSS (WebSocket Secure) instead of WS\n";
    file << "# Default: false\n";
    file << "signaling_use_ssl = false\n";
    file << "\n";
    file << "# Signaling URL (for external mode)\n";
    file << "# WebSocket URL for external signaling server (e.g., ws://127.0.0.1:8080)\n";
    file << "# Leave empty if using embedded mode\n";
    file << "signaling_url = \n";
    file << "\n";
    file << "# STUN server URL (optional)\n";
    file << "# STUN server for NAT traversal (e.g., stun:stun.l.google.com:19302)\n";
    file << "# Leave empty to disable STUN\n";
    file << "stun_server = \n";
    file << "\n";
    file << "# TURN server URL (optional)\n";
    file << "# TURN server for NAT traversal when STUN is insufficient\n";
    file << "# Format: turn:host:port\n";
    file << "# Leave empty to disable TURN\n";
    file << "turn_server = \n";
    file << "\n";
    file << "# TURN server username (optional)\n";
    file << "# Required if turn_server is specified\n";
    file << "turn_username = \n";
    file << "\n";
    file << "# TURN server password (optional)\n";
    file << "# Required if turn_server is specified\n";
    file << "turn_password = \n";
    file << "\n";
    file << "# ICE candidate timeout in milliseconds\n";
    file << "# Timeout for ICE candidate gathering\n";
    file << "# Default: 10000 (10 seconds)\n";
    file << "ice_timeout_ms = 10000\n";

    file.close();
}
