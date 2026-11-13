// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_config_parser.h"
#include "encoder_config.h"

#include <algorithm>
#include <cctype>
#include <gst/gst.h>
#include <ranges>
#include <sstream>

#include "../libvcai/include/logging.h"
#include "encoder_enums.h"
#include "gst_raii.h"
#include "roi/roi_parser.h"

GST_DEBUG_CATEGORY_STATIC(encoder_config_debug);
#define GST_CAT_DEFAULT encoder_config_debug

/**
 * @file encoder_config_parser.cpp
 * @brief Implementation of encoder configuration parsing.
 */

/**
 * @brief Initializes the GStreamer debug category for encoder configuration.
 */
void init_encoder_config_debug_category() {
    GST_DEBUG_CATEGORY_INIT(encoder_config_debug, "encoder-config", 0, "Encoder configuration loading and parsing");
}

namespace {
    [[nodiscard]] std::string read_config_string(GKeyFile *key_file, const char *group_name, const char *key_name,
                                                 GError *&error, const std::string &error_prefix);
    [[nodiscard]] std::optional<bool> read_config_boolean(GKeyFile *key_file, const char *group_name,
                                                          const char *key_name, GError *&error,
                                                          const std::string &error_prefix);
    [[nodiscard]] std::optional<gint> read_config_integer(GKeyFile *key_file, const char *group_name,
                                                          const char *key_name, GError *&error,
                                                          const std::string &error_prefix);
    [[nodiscard]] std::optional<gdouble> read_config_double(GKeyFile *key_file, const char *group_name,
                                                            const char *key_name, GError *&error,
                                                            const std::string &error_prefix);

    [[nodiscard]] std::optional<background_mode> parse_background_mode_string(const std::string &mode_str,
                                                                              const std::string &source_label) {
        if (mode_str.empty()) {
            return std::nullopt;
        }
        if (mode_str == "off" || mode_str == "background_blur" || mode_str == "qp_map_delta") {
            return string_to_background_mode(mode_str.c_str());
        }
        throw std::runtime_error(source_label + " must be one of: off, background_blur, qp_map_delta");
    }

    /**
     * @brief Helper function to split a comma-separated string into a vector.
     * @param str The comma-separated string.
     * @return A vector of strings.
     */
    std::vector<std::string> split_comma_separated(const std::string &str) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        while (std::getline(ss, item, ',')) {
            // Trim whitespace
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);
            if (!item.empty()) {
                result.push_back(item);
            }
        }
        return result;
    }

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
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_common_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Common")) {
            return;
        }

        const std::string input_type_str =
                read_config_string(key_file, "Common", "input_type", error, "Failed to read input_type");
        if (!input_type_str.empty()) {
            config.input_type_value = string_to_input_type(input_type_str.c_str());
        }

        if (const auto width = read_config_integer(key_file, "Common", "width", error, "Failed to read width")) {
            config.width = *width;
        }

        if (const auto height = read_config_integer(key_file, "Common", "height", error, "Failed to read height")) {
            config.height = *height;
        }

        if (const auto framerate =
                    read_config_integer(key_file, "Common", "framerate", error, "Failed to read framerate")) {
            config.framerate = *framerate;
        }

        if (has_group_and_key(key_file, "Common", "background_color")) {
            const gint64 bg_color = g_key_file_get_int64(key_file, "Common", "background_color", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read background_color: ") + error_msg);
            }
            config.background_color = static_cast<uint32_t>(bg_color);
        }
    }

    /**
     * @brief Loads output parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_output_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Output")) {
            return;
        }

        const std::string output_directory =
                read_config_string(key_file, "Output", "output_directory", error, "Failed to read output_directory");
        if (!output_directory.empty()) {
            config.output_directory = output_directory;
        }
    }

    /**
     * @brief Loads video properties from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_video_properties(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Video")) {
            return;
        }

        // Load file input parameters (only used when input_type = "file")
        if (has_group_and_key(key_file, "Video", "file_name")) {
            sf::unique_g_char_ptr file_name(g_key_file_get_string(key_file, "Video", "file_name", &error));
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read file_name: ") + error_msg);
            }
            if (file_name && strlen(file_name.get()) > 0) {
                config.file_name = std::move(file_name);
                sf::debug(encoder_config_debug, "Loaded file_name from config: %s", config.file_name.get());
            } else {
                sf::warning(encoder_config_debug, "file_name key exists in config but value is empty");
            }
        } else {
            sf::warning(encoder_config_debug, "file_name key not found in [Video] section of config file");
        }

        if (has_group_and_key(key_file, "Video", "format")) {
            sf::unique_g_char_ptr format_str(g_key_file_get_string(key_file, "Video", "format", &error));
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read format: ") + error_msg);
            }
            if (format_str && strlen(format_str.get()) > 0) {
                config.format = sf::string_to_video_format(format_str.get());
            }
        }

        if (has_group_and_key(key_file, "Video", "width")) {
            config.width = g_key_file_get_integer(key_file, "Video", "width", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read width: ") + error_msg);
            }
        }

        if (has_group_and_key(key_file, "Video", "height")) {
            config.height = g_key_file_get_integer(key_file, "Video", "height", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read height: ") + error_msg);
            }
        }

        if (has_group_and_key(key_file, "Video", "framerate")) {
            config.framerate = g_key_file_get_integer(key_file, "Video", "framerate", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read framerate: ") + error_msg);
            }
        }
    }

    /**
     * @brief Loads camera parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_camera_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Camera")) {
            return;
        }

        const std::string device =
                read_config_string(key_file, "Camera", "device", error, "Failed to read camera device");
        if (!device.empty()) {
            config.camera_device = device;
        }
    }

    /**
     * @brief Loads UDP parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_udp_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "UDP")) {
            return;
        }

        const std::string bind_address =
                read_config_string(key_file, "UDP", "bind_address", error, "Failed to read UDP bind_address");
        if (!bind_address.empty()) {
            config.udp.bind_address = bind_address;
        }

        const std::string encoding =
                read_config_string(key_file, "UDP", "encoding", error, "Failed to read UDP encoding");
        if (!encoding.empty()) {
            config.udp.encoding = encoding;
        }

        if (const auto udp_port = read_config_integer(key_file, "UDP", "port", error, "Failed to read UDP port")) {
            config.udp.port = static_cast<uint16_t>(*udp_port);
        }

        if (const auto payload_type =
                    read_config_integer(key_file, "UDP", "payload_type", error, "Failed to read UDP payload_type")) {
            config.udp.payload_type = static_cast<uint32_t>(*payload_type);
        }

        if (const auto clock_rate =
                    read_config_integer(key_file, "UDP", "clock_rate", error, "Failed to read UDP clock_rate")) {
            config.udp.clock_rate = static_cast<uint32_t>(*clock_rate);
        }

        if (const auto latency_ms =
                    read_config_integer(key_file, "UDP", "latency_ms", error, "Failed to read UDP latency_ms")) {
            config.udp.latency_ms = *latency_ms;
        }
    }

    /**
     * @brief Reads a string parameter from GKeyFile with error handling.
     * @param key_file GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Reference to GError pointer.
     * @param error_prefix Prefix for error message.
     * @return String value or empty string if key doesn't exist.
     * @throws std::runtime_error if reading fails.
     */
    [[nodiscard]] std::string read_config_string(GKeyFile *key_file, const char *group_name, const char *key_name,
                                                 GError *&error, const std::string &error_prefix) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return "";
        }

        const sf::unique_g_char_ptr value_str(g_key_file_get_string(key_file, group_name, key_name, &error));
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + ": " + error_msg);
        }
        return value_str ? value_str.get() : "";
    }

    /**
     * @brief Reads a boolean parameter from GKeyFile with error handling.
     * @param key_file GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Reference to GError pointer.
     * @param error_prefix Prefix for error message.
     * @return Boolean value or std::nullopt if key doesn't exist.
     * @throws std::runtime_error if reading fails.
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
            throw std::runtime_error(error_prefix + ": " + error_msg);
        }
        return static_cast<bool>(value);
    }

    /**
     * @brief Reads an integer parameter from GKeyFile with error handling.
     * @param key_file GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Reference to GError pointer.
     * @param error_prefix Prefix for error message.
     * @return Integer value or std::nullopt if key doesn't exist.
     * @throws std::runtime_error if reading fails.
     */
    [[nodiscard]] std::optional<gint> read_config_integer(GKeyFile *key_file, const char *group_name,
                                                          const char *key_name, GError *&error,
                                                          const std::string &error_prefix) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return std::nullopt;
        }

        const gint value = g_key_file_get_integer(key_file, group_name, key_name, &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + ": " + error_msg);
        }
        return value;
    }

    /**
     * @brief Reads a double parameter from GKeyFile with error handling.
     * @param key_file GKeyFile to read from.
     * @param group_name Group name.
     * @param key_name Key name.
     * @param error Reference to GError pointer.
     * @param error_prefix Prefix for error message.
     * @return Double value or std::nullopt if key doesn't exist.
     * @throws std::runtime_error if reading fails.
     */
    [[nodiscard]] std::optional<gdouble> read_config_double(GKeyFile *key_file, const char *group_name,
                                                            const char *key_name, GError *&error,
                                                            const std::string &error_prefix) {
        if (!has_group_and_key(key_file, group_name, key_name)) {
            return std::nullopt;
        }

        const gdouble value = g_key_file_get_double(key_file, group_name, key_name, &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(error_prefix + ": " + error_msg);
        }
        return value;
    }

    /**
     * @brief Loads ROI detection string parameters.
     * @param key_file GKeyFile to read from.
     * @param config Configuration to populate.
     * @param error Reference to GError pointer.
     */
    void load_roi_string_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        const std::string deepstream_config_file = read_config_string(key_file, "ROI", "deepstream_config_file", error,
                                                                      "Failed to read deepstream_config_file");
        if (!deepstream_config_file.empty()) {
            config.deepstream_config_file = deepstream_config_file;
        }

        const std::string labels_str = read_config_string(key_file, "ROI", "labels", error, "Failed to read labels");
        if (!labels_str.empty()) {
            config.labels = split_comma_separated(labels_str.c_str());
        }

        const std::string static_roi_json_file = read_config_string(key_file, "ROI", "static_roi_json_file", error,
                                                                    "Failed to read static_roi_json_file");
        if (!static_roi_json_file.empty()) {
            config.static_roi_json_file = static_roi_json_file;
        }

        const std::string groundtruth_roi_file = read_config_string(key_file, "ROI", "groundtruth_roi_file", error,
                                                                    "Failed to read groundtruth_roi_file");
        if (!groundtruth_roi_file.empty()) {
            config.groundtruth_roi_file = groundtruth_roi_file;
        }

        const std::string yolo_version_str =
                read_config_string(key_file, "ROI", "yolo_version", error, "Failed to read yolo_version");
        if (!yolo_version_str.empty()) {
            config.yolo_version = yolo_version_str;
        }
    }

    /**
     * @brief Loads ROI detection boolean parameters.
     * @param key_file GKeyFile to read from.
     * @param config Configuration to populate.
     * @param error Reference to GError pointer.
     */
    void load_roi_boolean_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        const auto inference_engine =
                read_config_boolean(key_file, "ROI", "inference_engine", error, "Failed to read inference_engine");
        if (inference_engine.has_value()) {
            config.inference_engine = *inference_engine;
        }

        const auto pack_rois = read_config_boolean(key_file, "ROI", "pack_rois", error, "Failed to read pack_rois");
        if (pack_rois.has_value()) {
            config.pack_rois = *pack_rois;
        }

        const auto enable_luma_adaptation = read_config_boolean(key_file, "ROI", "enable_luma_adaptation", error,
                                                                "Failed to read enable_luma_adaptation");
        if (enable_luma_adaptation.has_value()) {
            config.enable_luma_adaptation = *enable_luma_adaptation;
        }

        const auto enable_adaptive_luma_adaptation =
                read_config_boolean(key_file, "ROI", "enable_adaptive_luma_adaptation", error,
                                    "Failed to read enable_adaptive_luma_adaptation");
        if (enable_adaptive_luma_adaptation.has_value()) {
            config.enable_adaptive_luma_adaptation = *enable_adaptive_luma_adaptation;
        }

        const auto adaptive_luma_analyzer_on_keyframes_only =
                read_config_boolean(key_file, "ROI", "adaptive_luma_analyzer_on_keyframes_only", error,
                                    "Failed to read adaptive_luma_analyzer_on_keyframes_only");
        if (adaptive_luma_analyzer_on_keyframes_only.has_value()) {
            config.adaptive_luma_analyzer_on_keyframes_only = *adaptive_luma_analyzer_on_keyframes_only;
        }
    }

    void load_roi_adaptive_luma_path(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!has_group_and_key(key_file, "ROI", "input_analyzer_model_path")) {
            return;
        }
        const std::string path = read_config_string(key_file, "ROI", "input_analyzer_model_path", error,
                                                    "Failed to read input_analyzer_model_path");
        if (!path.empty()) {
            config.input_analyzer_model_path = path;
        }
    }

    void load_roi_background_mode_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (has_group_and_key(key_file, "ROI", "background_blur")) {
            throw std::runtime_error(
                    "ROI.background_blur is no longer supported. Use ROI.background_mode=background_blur or off.");
        }

        const std::string background_mode_str =
                read_config_string(key_file, "ROI", "background_mode", error, "Failed to read background_mode");
        if (const auto parsed_mode = parse_background_mode_string(background_mode_str, "ROI.background_mode")) {
            config.background_mode_value = *parsed_mode;
        }
    }

    /**
     * @brief Loads ROI detection integer parameters.
     * @param key_file GKeyFile to read from.
     * @param config Configuration to populate.
     * @param error Reference to GError pointer.
     */
    void load_roi_integer_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        const auto num_frames = read_config_integer(key_file, "ROI", "num_frames", error, "Failed to read num_frames");
        if (num_frames.has_value()) {
            config.num_frames = *num_frames;
        }

        const auto roi_fixed_margin_px = read_config_integer(key_file, "ROI", "roi_fixed_margin_px", error,
                                                             "Failed to read roi_fixed_margin_px");
        if (roi_fixed_margin_px.has_value()) {
            config.roi_fixed_margin_px = *roi_fixed_margin_px;
        }

        const auto temporal_resampling = read_config_integer(key_file, "ROI", "temporal_resampling", error,
                                                             "Failed to read temporal_resampling");
        if (temporal_resampling.has_value()) {
            config.temporal_resampling = *temporal_resampling;
        }

        const auto qp_map_background_delta = read_config_integer(key_file, "ROI", "qp_map_background_delta", error,
                                                                 "Failed to read qp_map_background_delta");
        if (qp_map_background_delta.has_value()) {
            config.qp_map_background_delta = *qp_map_background_delta;
        }

        const auto kernel_size =
                read_config_integer(key_file, "ROI", "kernel_size", error, "Failed to read kernel_size");
        if (kernel_size.has_value()) {
            config.kernel_size = *kernel_size;
        }
    }

    /**
     * @brief Loads ROI detection double parameters.
     * @param key_file GKeyFile to read from.
     * @param config Configuration to populate.
     * @param error Reference to GError pointer.
     */
    void load_roi_double_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        const auto sigma = read_config_double(key_file, "ROI", "sigma", error, "Failed to read sigma");
        if (sigma.has_value()) {
            config.sigma = *sigma;
        }

        const auto luma_downscale_factor = read_config_double(key_file, "ROI", "luma_downscale_factor", error,
                                                              "Failed to read luma_downscale_factor");
        if (luma_downscale_factor.has_value()) {
            config.luma_downscale_factor = *luma_downscale_factor;
        }
    }

    /**
     * @brief Loads ROI detection packer type parameter.
     * @param key_file GKeyFile to read from.
     * @param config Configuration to populate.
     * @param error Reference to GError pointer.
     */
    void load_roi_packer_type(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!has_group_and_key(key_file, "ROI", "packer_type")) {
            return;
        }

        gchar *packer_type_raw = g_key_file_get_string(key_file, "ROI", "packer_type", &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(std::string("Failed to read packer_type: ") + error_msg);
        }
        if (packer_type_raw) {
            config.packer_type_value = string_to_packer_type(packer_type_raw);
            g_free(packer_type_raw);
        }
    }

    /**
     * @brief Loads ROI margin mode parameter.
     * @param key_file GKeyFile to read from.
     * @param config Configuration to populate.
     * @param error Reference to GError pointer.
     */
    void load_roi_margin_mode(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!has_group_and_key(key_file, "ROI", "roi_margin_mode")) {
            return;
        }

        gchar *roi_margin_mode_raw = g_key_file_get_string(key_file, "ROI", "roi_margin_mode", &error);
        if (error) {
            const std::string error_msg = error->message;
            g_clear_error(&error);
            throw std::runtime_error(std::string("Failed to read roi_margin_mode: ") + error_msg);
        }
        if (roi_margin_mode_raw) {
            config.roi_margin_mode_value = string_to_roi_margin_mode(roi_margin_mode_raw);
            g_free(roi_margin_mode_raw);
        }
    }

    /**
     * @brief Loads ROI detection parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_roi_detection_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "ROI")) {
            return;
        }

        load_roi_string_parameters(key_file, config, error);
        load_roi_boolean_parameters(key_file, config, error);
        load_roi_background_mode_parameters(key_file, config, error);
        load_roi_integer_parameters(key_file, config, error);
        load_roi_double_parameters(key_file, config, error);
        load_roi_adaptive_luma_path(key_file, config, error);
        load_roi_packer_type(key_file, config, error);
        load_roi_margin_mode(key_file, config, error);

        config.effective_background_mode_value = config.background_mode_value;

        // Any active background mode disables ROI packing.
        if (config.background_mode_value != background_mode::off) {
            config.pack_rois = false;
        }
    }

    /**
     * @brief Loads tracker parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_tracker_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Tracker")) {
            return;
        }

        const std::string tracker_config_file = read_config_string(key_file, "Tracker", "tracker_config_file", error,
                                                                   "Failed to read tracker_config_file");
        if (!tracker_config_file.empty()) {
            config.tracker.config_file = tracker_config_file;
        }

        const std::string tracker_type =
                read_config_string(key_file, "Tracker", "tracker_type", error, "Failed to read tracker_type");
        if (!tracker_type.empty()) {
            config.tracker.type = tracker_type;
        }

        if (const auto value = read_config_integer(key_file, "Tracker", "tracker_max_shadow_tracking_age", error,
                                                   "Failed to read tracker_max_shadow_tracking_age")) {
            config.tracker.max_shadow_tracking_age = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Tracker", "tracker_enable_reassoc", error,
                                                   "Failed to read tracker_enable_reassoc")) {
            config.tracker.enable_reassoc = *value;
        }
        if (const auto value = read_config_integer(key_file, "Tracker", "tracker_trajectory_projection_length", error,
                                                   "Failed to read tracker_trajectory_projection_length")) {
            config.tracker.trajectory_projection_length = *value;
        }
        if (const auto value = read_config_integer(key_file, "Tracker", "tracker_max_tracklet_matching_time_range",
                                                   error, "Failed to read tracker_max_tracklet_matching_time_range")) {
            config.tracker.max_tracklet_matching_time_range = *value;
        }
        if (const auto value = read_config_integer(key_file, "Tracker", "tracker_max_targets_per_stream", error,
                                                   "Failed to read tracker_max_targets_per_stream")) {
            config.tracker.max_targets_per_stream = *value;
        }
        if (const auto value = read_config_integer(key_file, "Tracker", "tracker_probation_age", error,
                                                   "Failed to read tracker_probation_age")) {
            config.tracker.probation_age = *value;
        }
        if (const auto value = read_config_integer(key_file, "Tracker", "tracker_early_termination_age", error,
                                                   "Failed to read tracker_early_termination_age")) {
            config.tracker.early_termination_age = *value;
        }
        if (const auto value = read_config_double(key_file, "Tracker", "tracker_min_confidence", error,
                                                  "Failed to read tracker_min_confidence")) {
            config.tracker.min_confidence = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Tracker", "tracker_use_prediction_for_assoc", error,
                                                   "Failed to read tracker_use_prediction_for_assoc")) {
            config.tracker.use_prediction_for_assoc = *value;
        }
    }

    /**
     * @brief Loads detection parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_detection_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Detection")) {
            return;
        }

        if (has_group_and_key(key_file, "Detection", "base_confidence_threshold")) {
            config.detection.base_confidence_threshold = static_cast<float>(
                    g_key_file_get_double(key_file, "Detection", "base_confidence_threshold", &error));
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read base_confidence_threshold: ") + error_msg);
            }
        }

        if (has_group_and_key(key_file, "Detection", "maximum_missing_frames")) {
            config.detection.maximum_missing_frames = static_cast<uint32_t>(
                    g_key_file_get_integer(key_file, "Detection", "maximum_missing_frames", &error));
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read maximum_missing_frames: ") + error_msg);
            }
        }

        if (has_group_and_key(key_file, "Detection", "enable_prediction")) {
            config.detection.enable_prediction =
                    g_key_file_get_boolean(key_file, "Detection", "enable_prediction", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read enable_prediction: ") + error_msg);
            }
        }

        if (has_group_and_key(key_file, "Detection", "roi_accumulation_mode")) {
            const std::string mode_str = read_config_string(key_file, "Detection", "roi_accumulation_mode", error,
                                                            "Failed to read roi_accumulation_mode");
            if (!mode_str.empty()) {
                config.detection.roi_accumulation_mode_value = string_to_roi_accumulation_mode(mode_str.c_str());
            }
        }

        if (has_group_and_key(key_file, "Detection", "roi_overlap_threshold")) {
            config.detection.roi_overlap_threshold =
                    static_cast<float>(g_key_file_get_double(key_file, "Detection", "roi_overlap_threshold", &error));
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read roi_overlap_threshold: ") + error_msg);
            }
        }

        if (has_group_and_key(key_file, "Detection", "enable_tracker")) {
            config.detection.enable_tracker = g_key_file_get_boolean(key_file, "Detection", "enable_tracker", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read enable_tracker: ") + error_msg);
            }
        }
    }

    /**
     * @brief Loads stabilization parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_stabilization_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Stabilization")) {
            return;
        }

        const auto enabled = read_config_boolean(key_file, "Stabilization", "enabled", error,
                                                 "Failed to read stabilization enabled");
        if (enabled.has_value()) {
            config.stabilization.enabled = *enabled;
        }

        const std::string mode_str =
                read_config_string(key_file, "Stabilization", "mode", error, "Failed to read stabilization mode");
        if (!mode_str.empty()) {
            config.stabilization.mode = string_to_stabilization_mode(mode_str.c_str());
        }

        const auto alpha =
                read_config_double(key_file, "Stabilization", "alpha", error, "Failed to read stabilization alpha");
        if (alpha.has_value()) {
            config.stabilization.alpha = static_cast<float>(*alpha);
        }

        const auto alpha_size = read_config_double(key_file, "Stabilization", "alpha_size", error,
                                                   "Failed to read stabilization alpha_size");
        if (alpha_size.has_value()) {
            config.stabilization.alpha_size = static_cast<float>(*alpha_size);
        }

        const auto min_position_delta = read_config_integer(key_file, "Stabilization", "min_position_delta", error,
                                                            "Failed to read stabilization min_position_delta");
        if (min_position_delta.has_value()) {
            config.stabilization.min_position_delta = *min_position_delta;
        }

        const auto min_size_delta = read_config_integer(key_file, "Stabilization", "min_size_delta", error,
                                                        "Failed to read stabilization min_size_delta");
        if (min_size_delta.has_value()) {
            config.stabilization.min_size_delta = *min_size_delta;
        }

        const auto velocity_factor = read_config_double(key_file, "Stabilization", "velocity_adaptive_factor", error,
                                                        "Failed to read stabilization velocity_adaptive_factor");
        if (velocity_factor.has_value()) {
            config.stabilization.velocity_adaptive_factor = static_cast<float>(*velocity_factor);
        }
    }

    /**
     * @brief Loads encoder rate control parameters from the configuration file.
     *
     * Reads the optional [Encoder] section which controls encoder rate control mode.
     * When the "qp" key is set, constant-QP mode is used instead of the default CBR.
     *
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_encoder_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Encoder")) {
            return;
        }

        const auto qp_value = read_config_integer(key_file, "Encoder", "qp", error, "Failed to read Encoder qp");
        if (qp_value.has_value()) {
            if (*qp_value < 0 || *qp_value > 51) {
                throw std::runtime_error("Encoder qp must be 0-51, got " + std::to_string(*qp_value));
            }
            config.encoder_qp = *qp_value;
            sf::info(encoder_config_debug, "Loaded encoder QP from config: %d (constant-QP mode)", *qp_value);
        }

        const std::string element_str =
                read_config_string(key_file, "Encoder", "element", error, "Failed to read Encoder element");
        if (!element_str.empty()) {
            config.encoder_element = element_str;
            sf::info(encoder_config_debug, "Loaded encoder element from config: %s", element_str.c_str());
        }
    }

    /**
     * @brief Loads performance parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_performance_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Performance")) {
            return;
        }

        if (const auto value = read_config_boolean(key_file, "Performance", "enable_frame_dropping", error,
                                                   "Failed to read enable_frame_dropping from config")) {
            config.performance.enable_frame_dropping = *value;
        }
        if (const auto value = read_config_double(key_file, "Performance", "target_fps", error,
                                                  "Failed to read target_fps from config")) {
            config.performance.target_fps = *value;
        }
        if (const auto value = read_config_double(key_file, "Performance", "drop_smoothing_factor", error,
                                                  "Failed to read drop_smoothing_factor from config")) {
            config.performance.drop_smoothing_factor = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Performance", "enable_deepstream_bypass", error,
                                                   "Failed to read enable_deepstream_bypass from config")) {
            config.performance.enable_deepstream_bypass = *value;
        }
        if (const auto value = read_config_integer(key_file, "Performance", "bypass_interval", error,
                                                   "Failed to read bypass_interval from config")) {
            config.performance.bypass_interval = *value;
        }
        if (const auto value = read_config_integer(key_file, "Performance", "merger_buffer_size", error,
                                                   "Failed to read merger_buffer_size from config")) {
            config.performance.merger_buffer_size = static_cast<size_t>(*value);
        }
        if (const auto value = read_config_integer(key_file, "Performance", "merger_max_latency_ms", error,
                                                   "Failed to read merger_max_latency_ms from config")) {
            config.performance.merger_max_latency_ms = static_cast<uint32_t>(*value);
        }
    }

    /**
     * @brief Loads debug parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_debug_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Debug")) {
            return;
        }

        if (const auto value = read_config_boolean(key_file, "Debug", "draw_borders", error,
                                                   "Failed to read draw_borders from config")) {
            config.debug.draw_borders = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Debug", "draw_rtp_timestamp", error,
                                                   "Failed to read draw_rtp_timestamp from config")) {
            config.debug.draw_rtp_timestamp = *value;
        }
        if (const auto value =
                    read_config_boolean(key_file, "Debug", "draw_fps", error, "Failed to read draw_fps from config")) {
            config.debug.draw_fps = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Debug", "draw_bitrate", error,
                                                   "Failed to read draw_bitrate from config")) {
            config.debug.draw_bitrate = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Debug", "draw_bitrate_large", error,
                                                   "Failed to read draw_bitrate_large from config")) {
            config.debug.draw_bitrate_large = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Debug", "save_frames", error,
                                                   "Failed to read save_frames from config")) {
            config.debug.save_frames = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Debug", "enable_display_branch", error,
                                                   "Failed to read enable_display_branch from config")) {
            config.debug.enable_display_branch = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Debug", "enable_mp4_branch", error,
                                                   "Failed to read enable_mp4_branch from config")) {
            config.debug.enable_mp4_branch = *value;
        }
        if (const auto value = read_config_boolean(key_file, "Debug", "enable_h265_branch", error,
                                                   "Failed to read enable_h265_branch from config")) {
            config.debug.enable_h265_branch = *value;
        }
    }

    /**
     * @brief Loads logging parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_logging_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Logging")) {
            return;
        }

        if (has_group_and_key(key_file, "Logging", "enable_status_reporter")) {
            config.enable_status_reporter =
                    g_key_file_get_boolean(key_file, "Logging", "enable_status_reporter", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read enable_status_reporter from config: ") +
                                         error_msg);
            }
        }
        if (has_group_and_key(key_file, "Logging", "packer_logging")) {
            config.packer_logging = g_key_file_get_boolean(key_file, "Logging", "packer_logging", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read packer_logging from config: ") + error_msg);
            }
        }

        if (has_group_and_key(key_file, "Logging", "enable_performance_monitoring")) {
            config.enable_performance_monitoring =
                    g_key_file_get_boolean(key_file, "Logging", "enable_performance_monitoring", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read enable_performance_monitoring from config: ") +
                                         error_msg);
            }
        }

        if (has_group_and_key(key_file, "Logging", "performance_monitoring_interval_seconds")) {
            config.performance_monitoring_interval_seconds =
                    g_key_file_get_integer(key_file, "Logging", "performance_monitoring_interval_seconds", &error);
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(
                        std::string("Failed to read performance_monitoring_interval_seconds from config: ") +
                        error_msg);
            }
        }
    }

    /**
     * @brief Logs ROI information for debugging purposes.
     * @param info The ROI sequence information to log.
     */
    void log_roi_info(const roi_sequence_info &info) {
        sf::info(encoder_config_debug, "--- Parsed ROI Info ---");
        sf::info(encoder_config_debug, "  Sequence Name: %s", info.sequence_name.c_str());
        sf::info(encoder_config_debug, "  Dimensions: %d x %d", info.width, info.height);
        sf::info(encoder_config_debug, "  Nominal Framerate: %d", info.frame_rate_nominal);
        sf::info(encoder_config_debug, "  ROI frames loaded: %zu", info.rois.size());

        if (info.frame_rate_details) {
            const auto &frd = *info.frame_rate_details;
            sf::info(encoder_config_debug, "  Frame Rate Details (from sfu_config):");
            sf::info(encoder_config_debug, "    Intra Period: %d", frd.intra_period);
            sf::info(encoder_config_debug, "    Frame Rate: %d", frd.frame_rate);
            sf::info(encoder_config_debug, "    Frames to Encode: %d", frd.frames_to_be_encoded);
            sf::info(encoder_config_debug, "    Frame Skip: %d", frd.frame_skip);
        } else {
            sf::info(encoder_config_debug, "  Frame Rate Details: Not found in sfu_config.");
        }

        if (info.qp_config_ld) {
            sf::info(encoder_config_debug, "  Low-Delay QP Config: Found");
        } else {
            sf::info(encoder_config_debug, "  Low-Delay QP Config: Not found");
        }

        if (info.qp_config_ai) {
            sf::info(encoder_config_debug, "  All-Intra QP Config: Found");
        } else {
            sf::info(encoder_config_debug, "  All-Intra QP Config: Not found");
        }
        sf::info(encoder_config_debug, "-----------------------");
    }

    /**
     * @brief Loads and processes ROI data file if specified.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_roi_data_file(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "ROI")) {
            return;
        }

        if (has_group_and_key(key_file, "ROI", "data_file")) {
            const sf::unique_g_char_ptr roi_data_path(g_key_file_get_string(key_file, "ROI", "data_file", &error));
            if (error) {
                const std::string error_msg = error->message;
                g_clear_error(&error);
                throw std::runtime_error(std::string("Failed to read data_file: ") + error_msg);
            }

            // Only try to load ROI file if the path is not empty
            if (strlen(roi_data_path.get()) > 0) {
                try {
                    auto roi_info_opt = load_roi_file(roi_data_path.get(), config.temporal_resampling);
                    if (!roi_info_opt) {
                        throw std::runtime_error("Failed to load ROI data file");
                    }
                    config.roi_info = std::move(*roi_info_opt);
                    log_roi_info(*config.roi_info);

                    // Override video properties with ROI file data
                    config.width = config.roi_info->width;
                    config.height = config.roi_info->height;
                    config.framerate = config.roi_info->frame_rate_nominal;
                } catch (const std::exception &e) {
                    throw std::runtime_error(std::string("Failed to parse ROI data file: ") + e.what());
                }
            }
        }
    }

    /**
     * @brief Loads Communication parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_communication_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "Communication")) {
            return;
        }

        const std::string transport_str = read_config_string(key_file, "Communication", "transport_type", error,
                                                             "Failed to read Communication transport_type");
        if (!transport_str.empty()) {
            if (transport_str == "rtp") {
                config.communication.transport_type = sf::communication::transport_type::udp;
            } else if (transport_str == "webrtc") {
                config.communication.transport_type = sf::communication::transport_type::webrtc;
            } else {
                throw std::runtime_error("Invalid communication transport type: '" + transport_str +
                                         "'. Use 'rtp' or 'webrtc'.");
            }
        }

        const std::string host =
                read_config_string(key_file, "Communication", "host", error, "Failed to read Communication host");
        if (!host.empty()) {
            config.communication.host = host;
        }

        if (const auto port = read_config_integer(key_file, "Communication", "port", error,
                                                  "Failed to read Communication port")) {
            config.communication.port = static_cast<uint16_t>(*port);
        }

        if (const auto timeout = read_config_integer(key_file, "Communication", "timeout_ms", error,
                                                     "Failed to read Communication timeout_ms")) {
            config.communication.timeout_ms = static_cast<uint32_t>(*timeout);
        }
    }

    /**
     * @brief Loads WebRTC-specific parameters from the configuration file.
     * @param key_file The GKeyFile to read from.
     * @param config The encoder_config to populate.
     * @param error Pointer to GError for error handling.
     */
    void load_webrtc_parameters(GKeyFile *key_file, encoder_config &config, GError *&error) {
        if (!g_key_file_has_group(key_file, "WebRTC")) {
            return;
        }

        const std::string signaling_url =
                read_config_string(key_file, "WebRTC", "signaling_url", error, "Failed to read WebRTC signaling_url");
        if (!signaling_url.empty()) {
            config.webrtc.signaling_url = signaling_url;
        }

        const std::string stun_server =
                read_config_string(key_file, "WebRTC", "stun_server", error, "Failed to read WebRTC stun_server");
        if (!stun_server.empty()) {
            config.webrtc.stun_server = stun_server;
        }

        const std::string turn_server =
                read_config_string(key_file, "WebRTC", "turn_server", error, "Failed to read WebRTC turn_server");
        if (!turn_server.empty()) {
            config.webrtc.turn_server = turn_server;
        }

        const std::string turn_username =
                read_config_string(key_file, "WebRTC", "turn_username", error, "Failed to read WebRTC turn_username");
        if (!turn_username.empty()) {
            config.webrtc.turn_username = turn_username;
        }

        const std::string turn_password =
                read_config_string(key_file, "WebRTC", "turn_password", error, "Failed to read WebRTC turn_password");
        if (!turn_password.empty()) {
            config.webrtc.turn_password = turn_password;
        }

        if (const auto ice_timeout = read_config_integer(key_file, "WebRTC", "ice_timeout_ms", error,
                                                         "Failed to read WebRTC ice_timeout_ms")) {
            config.webrtc.ice_timeout_ms = static_cast<uint32_t>(*ice_timeout);
        }

        if (const auto enable_data_channels = read_config_boolean(key_file, "WebRTC", "enable_data_channels", error,
                                                                  "Failed to read WebRTC enable_data_channels")) {
            config.webrtc.enable_data_channels = *enable_data_channels;
        }
    }
} // namespace

/**
 * @brief Applies every configuration section to the given configuration.
 *
 * Reads all parameter groups from the loaded key file and populates config,
 * overriding video properties from the ROI data file last.
 *
 * @param key_file The GKeyFile to read from (must already be loaded).
 * @param config The encoder_config to populate.
 * @param error Pointer to GError for error handling.
 * @throws std::runtime_error if any section fails to parse.
 */
void parse_config_sections(GKeyFile *key_file, encoder_config &config, GError *&error) {
    // Load all configuration sections
    load_common_parameters(key_file, config, error);
    load_output_parameters(key_file, config, error);
    load_video_properties(key_file, config, error);
    load_camera_parameters(key_file, config, error);
    load_udp_parameters(key_file, config, error);
    load_encoder_parameters(key_file, config, error);
    load_roi_detection_parameters(key_file, config, error);
    load_tracker_parameters(key_file, config, error);
    load_detection_parameters(key_file, config, error);
    load_stabilization_parameters(key_file, config, error);
    load_performance_parameters(key_file, config, error);
    load_debug_parameters(key_file, config, error);
    load_logging_parameters(key_file, config, error);
    load_communication_parameters(key_file, config, error);
    load_webrtc_parameters(key_file, config, error);
    load_roi_data_file(key_file, config, error);
}
