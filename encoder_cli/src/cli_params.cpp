// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "cli_params.h"

#include <limits>
#include <string>

#include "libvcai/include/logging.h"

namespace {
    struct raw_command_line_values {
        gchar *config_file = nullptr;
        gchar *input_type = nullptr;
        gchar *output_directory = nullptr;
        gchar *file_name = nullptr;
        gchar *format = nullptr;
        gint width = 0;
        gint height = 0;
        gint framerate = 0;
        guint32 background_color = 0xFFFFFFFFU;
        gchar *camera_device = nullptr;
        gchar *udp_bind_address = nullptr;
        gint udp_port = 0;
        gchar *udp_encoding = nullptr;
        gint udp_payload_type = -1;
        gint udp_clock_rate = 0;
        gint udp_latency_ms = -1;
        gchar *deepstream_config_file = nullptr;
        gchar *roi_labels = nullptr;
        gchar *roi_data_file = nullptr;
        gchar *static_roi_json_file = nullptr;
        gchar *groundtruth_roi_file = nullptr;
        gboolean inference_engine = FALSE;
        gchar *pack_rois = nullptr;
        gchar *packer_type = nullptr;
        gint num_frames = 0;
        gint roi_fixed_margin_px = -1;
        gchar *roi_margin_mode = nullptr;
        gint temporal_resampling = 0;
        gchar *background_mode = nullptr;
        gint qp_map_background_delta = std::numeric_limits<gint>::min();
        gchar *enable_luma_adaptation = nullptr;
        gdouble luma_downscale_factor = -1.0;
        gchar *enable_adaptive_luma_adaptation = nullptr;
        gchar *input_analyzer_model_path = nullptr;
        gchar *adaptive_luma_analyzer_on_keyframes_only = nullptr;
        gdouble sigma = 0.0;
        gint kernel_size = 0;
        gchar *tracker_config_file = nullptr;
        gchar *tracker_type = nullptr;
        gint tracker_max_shadow_tracking_age = -1;
        gboolean tracker_enable_reassoc = FALSE;
        gint tracker_trajectory_projection_length = -1;
        gint tracker_max_tracklet_matching_time_range = -1;
        gdouble base_confidence_threshold = -1.0;
        gint maximum_missing_frames = -1;
        gboolean enable_prediction = FALSE;
        gchar *stabilization_enabled = nullptr;
        gchar *stabilization_mode = nullptr;
        gdouble stabilization_alpha = -1.0;
        gdouble stabilization_alpha_size = -1.0;
        gint stabilization_min_position_delta = -1;
        gint stabilization_min_size_delta = -1;
        gdouble stabilization_velocity_adaptive_factor = -1.0;
        gint encoder_qp = -1;
        gchar *encoder_element = nullptr;
        gboolean draw_borders = FALSE;
        gboolean draw_rtp_timestamp = FALSE;
        gboolean draw_fps = FALSE;
        gboolean draw_bitrate = FALSE;
        gboolean draw_bitrate_large = FALSE;
        gboolean save_frames = FALSE;
        gboolean enable_display_branch = FALSE;
        gboolean enable_mp4_branch = FALSE;
        gboolean enable_h265_branch = FALSE;
        gboolean debug_all = FALSE;
        gboolean enable_status_reporter = FALSE;
        gboolean packer_logging = FALSE;
        gchar *communication_transport_type = nullptr;
        gchar *communication_host = nullptr;
        gint communication_port = 0;
        gint communication_timeout_ms = 0;
        gchar *webrtc_signaling_url = nullptr;
        gchar *webrtc_stun_server = nullptr;
        gchar *webrtc_turn_server = nullptr;
        gchar *webrtc_turn_username = nullptr;
        gchar *webrtc_turn_password = nullptr;
        gint webrtc_ice_timeout_ms = 0;
        gboolean webrtc_enable_data_channels = FALSE;

        ~raw_command_line_values() {
            g_free(config_file);
            g_free(input_type);
            g_free(output_directory);
            g_free(file_name);
            g_free(format);
            g_free(camera_device);
            g_free(udp_bind_address);
            g_free(udp_encoding);
            g_free(deepstream_config_file);
            g_free(roi_labels);
            g_free(roi_data_file);
            g_free(static_roi_json_file);
            g_free(groundtruth_roi_file);
            g_free(pack_rois);
            g_free(packer_type);
            g_free(roi_margin_mode);
            g_free(background_mode);
            g_free(enable_luma_adaptation);
            g_free(tracker_config_file);
            g_free(tracker_type);
            g_free(stabilization_enabled);
            g_free(stabilization_mode);
            g_free(communication_transport_type);
            g_free(communication_host);
            g_free(webrtc_signaling_url);
            g_free(webrtc_stun_server);
            g_free(webrtc_turn_server);
            g_free(webrtc_turn_username);
            g_free(webrtc_turn_password);
            g_free(encoder_element);
        }
    };

    [[nodiscard]] std::optional<std::string> take_string(gchar *&value) {
        if (value == nullptr) {
            return std::nullopt;
        }
        std::string result = value;
        g_free(value);
        value = nullptr;
        return result;
    }

    [[nodiscard]] std::optional<bool> parse_explicit_boolean(const char *option_name, gchar *&value) {
        const auto raw_value = take_string(value);
        if (!raw_value.has_value()) {
            return std::nullopt;
        }

        if (*raw_value == "true" || *raw_value == "1" || *raw_value == "yes") {
            return true;
        }
        if (*raw_value == "false" || *raw_value == "0" || *raw_value == "no") {
            return false;
        }

        sf::error(nullptr, "Invalid value for --%s: '%s'. Use 'true' or 'false'.", option_name, raw_value->c_str());
        exit(1);
    }

    [[nodiscard]] uint16_t parse_uint16_or_exit(const char *option_name, const gint value) {
        if (value < 0 || value > std::numeric_limits<uint16_t>::max()) {
            sf::error(nullptr, "Invalid value for --%s: %d (must be 0-%u)", option_name, value,
                      static_cast<unsigned>(std::numeric_limits<uint16_t>::max()));
            exit(1);
        }
        return static_cast<uint16_t>(value);
    }

    [[nodiscard]] uint32_t parse_uint32_or_exit(const char *option_name, const gint value) {
        if (value < 0) {
            sf::error(nullptr, "Invalid value for --%s: %d (must be >= 0)", option_name, value);
            exit(1);
        }
        return static_cast<uint32_t>(value);
    }

    [[nodiscard]] background_mode parse_background_mode_or_exit(const std::string &value) {
        if (value == "off" || value == "background_blur" || value == "qp_map_delta") {
            return string_to_background_mode(value.c_str());
        }

        sf::error(nullptr,
                  "Invalid value for --roi-background-mode: '%s'. Use 'off', 'background_blur', or "
                  "'qp_map_delta' (x86 desktop only).",
                  value.c_str());
        exit(1);
    }

    void parse_common_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        const auto input_type = take_string(raw.input_type);
        if (input_type.has_value()) {
            params.common.input_type_value = string_to_input_type(input_type->c_str());
        }
        params.common.output_directory = take_string(raw.output_directory);
    }

    void parse_video_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        params.video.file_name = take_string(raw.file_name);

        const auto format = take_string(raw.format);
        if (format.has_value()) {
            params.video.format = sf::string_to_video_format(format->c_str());
        }

        if (raw.width > 0) {
            params.video.width = raw.width;
        }
        if (raw.height > 0) {
            params.video.height = raw.height;
        }
        if (raw.framerate > 0) {
            params.video.framerate = raw.framerate;
        }
        if (raw.background_color != 0xFFFFFFFFU) {
            params.video.background_color = raw.background_color;
        }
    }

    void parse_camera_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        params.camera.device = take_string(raw.camera_device);
    }

    void parse_udp_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        params.udp.bind_address = take_string(raw.udp_bind_address);

        if (raw.udp_port > 0) {
            params.udp.port = parse_uint16_or_exit("udp-port", raw.udp_port);
        }

        params.udp.encoding = take_string(raw.udp_encoding);

        if (raw.udp_payload_type >= 0) {
            params.udp.payload_type = parse_uint32_or_exit("udp-payload-type", raw.udp_payload_type);
        }
        if (raw.udp_clock_rate > 0) {
            params.udp.clock_rate = parse_uint32_or_exit("udp-clock-rate", raw.udp_clock_rate);
        }
        if (raw.udp_latency_ms >= 0) {
            params.udp.latency_ms = raw.udp_latency_ms;
        }
    }

    void parse_roi_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        params.roi.deepstream_config_file = take_string(raw.deepstream_config_file);
        params.roi.labels = take_string(raw.roi_labels);
        params.roi.data_file = take_string(raw.roi_data_file);
        params.roi.static_roi_json_file = take_string(raw.static_roi_json_file);
        params.roi.groundtruth_roi_file = take_string(raw.groundtruth_roi_file);

        if (raw.inference_engine != FALSE) {
            params.roi.inference_engine = true;
        }
        params.roi.pack_rois = parse_explicit_boolean("roi-pack-rois", raw.pack_rois);

        const auto packer_type = take_string(raw.packer_type);
        if (packer_type.has_value()) {
            params.roi.packer_type_value = string_to_packer_type(packer_type->c_str());
        }

        if (raw.num_frames > 0) {
            params.roi.num_frames = raw.num_frames;
        }
        if (raw.roi_fixed_margin_px >= 0) {
            params.roi.roi_fixed_margin_px = raw.roi_fixed_margin_px;
        }
        const auto roi_margin_mode = take_string(raw.roi_margin_mode);
        if (roi_margin_mode.has_value()) {
            params.roi.roi_margin_mode_value = string_to_roi_margin_mode(roi_margin_mode->c_str());
        }
        if (raw.temporal_resampling > 0) {
            params.roi.temporal_resampling = raw.temporal_resampling;
        }

        const auto background_mode = take_string(raw.background_mode);
        if (background_mode.has_value()) {
            params.roi.background_mode_value = parse_background_mode_or_exit(*background_mode);
        }
        if (raw.qp_map_background_delta != std::numeric_limits<gint>::min()) {
            params.roi.qp_map_background_delta = raw.qp_map_background_delta;
        }
        params.roi.enable_luma_adaptation =
                parse_explicit_boolean("enable-luma-adaptation", raw.enable_luma_adaptation);
        if (raw.luma_downscale_factor >= 0.0) {
            params.roi.luma_downscale_factor = raw.luma_downscale_factor;
        }
        params.roi.enable_adaptive_luma_adaptation =
                parse_explicit_boolean("enable-adaptive-luma-adaptation", raw.enable_adaptive_luma_adaptation);
        if (raw.input_analyzer_model_path != nullptr && raw.input_analyzer_model_path[0] != '\0') {
            params.roi.input_analyzer_model_path = std::string(raw.input_analyzer_model_path);
        }
        params.roi.adaptive_luma_analyzer_on_keyframes_only = parse_explicit_boolean(
                "adaptive-luma-analyzer-on-keyframes-only", raw.adaptive_luma_analyzer_on_keyframes_only);

        if (raw.sigma > 0.0) {
            params.roi.sigma = raw.sigma;
        }
        if (raw.kernel_size > 0) {
            params.roi.kernel_size = raw.kernel_size;
        }
    }

    void parse_tracker_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        params.tracker.config_file = take_string(raw.tracker_config_file);
        params.tracker.type = take_string(raw.tracker_type);

        if (raw.tracker_max_shadow_tracking_age >= 0) {
            params.tracker.max_shadow_tracking_age = raw.tracker_max_shadow_tracking_age;
        }
        if (raw.tracker_enable_reassoc != FALSE) {
            params.tracker.enable_reassoc = true;
        }
        if (raw.tracker_trajectory_projection_length >= 0) {
            params.tracker.trajectory_projection_length = raw.tracker_trajectory_projection_length;
        }
        if (raw.tracker_max_tracklet_matching_time_range >= 0) {
            params.tracker.max_tracklet_matching_time_range = raw.tracker_max_tracklet_matching_time_range;
        }
    }

    void parse_detection_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        if (raw.base_confidence_threshold >= 0.0) {
            params.detection.base_confidence_threshold = static_cast<float>(raw.base_confidence_threshold);
        }
        if (raw.maximum_missing_frames >= 0) {
            params.detection.maximum_missing_frames = raw.maximum_missing_frames;
        }
        if (raw.enable_prediction != FALSE) {
            params.detection.enable_prediction = true;
        }
    }

    void parse_stabilization_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        params.stabilization.enabled = parse_explicit_boolean("stabilization-enabled", raw.stabilization_enabled);

        const auto stabilization_mode = take_string(raw.stabilization_mode);
        if (stabilization_mode.has_value()) {
            params.stabilization.mode = string_to_stabilization_mode(stabilization_mode->c_str());
        }

        if (raw.stabilization_alpha >= 0.0) {
            params.stabilization.alpha = static_cast<float>(raw.stabilization_alpha);
        }
        if (raw.stabilization_alpha_size >= 0.0) {
            params.stabilization.alpha_size = static_cast<float>(raw.stabilization_alpha_size);
        }
        if (raw.stabilization_min_position_delta >= 0) {
            params.stabilization.min_position_delta = raw.stabilization_min_position_delta;
        }
        if (raw.stabilization_min_size_delta >= 0) {
            params.stabilization.min_size_delta = raw.stabilization_min_size_delta;
        }
        if (raw.stabilization_velocity_adaptive_factor >= 0.0) {
            params.stabilization.velocity_adaptive_factor =
                    static_cast<float>(raw.stabilization_velocity_adaptive_factor);
        }
    }

    void parse_encoder_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        if (raw.encoder_qp >= 0) {
            if (raw.encoder_qp > 51) {
                sf::error(nullptr, "Error: --encoder-qp must be 0-51, got %d", raw.encoder_qp);
                exit(1);
            }
            params.encoder.qp = raw.encoder_qp;
        }
        if (raw.encoder_element != nullptr && raw.encoder_element[0] != '\0') {
            params.encoder.element = std::string(raw.encoder_element);
        }
    }

    void parse_debug_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        if (raw.debug_all != FALSE) {
            params.debug.debug_all = true;
            params.debug.draw_borders = true;
            params.debug.draw_rtp_timestamp = true;
            params.debug.draw_fps = true;
            params.debug.draw_bitrate = true;
            params.debug.save_frames = true;
            params.debug.enable_display_branch = true;
            params.debug.enable_mp4_branch = true;
            params.debug.enable_h265_branch = true;
            params.logging.packer_logging = true;
            return;
        }

        if (raw.draw_borders != FALSE) {
            params.debug.draw_borders = true;
        }
        if (raw.draw_rtp_timestamp != FALSE) {
            params.debug.draw_rtp_timestamp = true;
        }
        if (raw.draw_fps != FALSE) {
            params.debug.draw_fps = true;
        }
        if (raw.draw_bitrate != FALSE) {
            params.debug.draw_bitrate = true;
        }
        if (raw.draw_bitrate_large != FALSE) {
            params.debug.draw_bitrate_large = true;
        }
        if (raw.save_frames != FALSE) {
            params.debug.save_frames = true;
        }
        if (raw.enable_display_branch != FALSE) {
            params.debug.enable_display_branch = true;
        }
        if (raw.enable_mp4_branch != FALSE) {
            params.debug.enable_mp4_branch = true;
        }
        if (raw.enable_h265_branch != FALSE) {
            params.debug.enable_h265_branch = true;
        }
    }

    void parse_logging_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        if (raw.enable_status_reporter != FALSE) {
            params.logging.enable_status_reporter = true;
        }
        if (raw.packer_logging != FALSE) {
            params.logging.packer_logging = true;
        }
    }

    void parse_communication_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        const auto transport_type = take_string(raw.communication_transport_type);
        if (transport_type.has_value()) {
            if (*transport_type == "rtp") {
                params.communication.transport_type = sf::communication::transport_type::udp;
            } else if (*transport_type == "webrtc") {
                params.communication.transport_type = sf::communication::transport_type::webrtc;
            } else {
                sf::error(nullptr, "Invalid communication transport type: '%s'. Use 'rtp' or 'webrtc'.",
                          transport_type->c_str());
                exit(1);
            }
        }

        params.communication.host = take_string(raw.communication_host);

        if (raw.communication_port > 0) {
            params.communication.port = parse_uint16_or_exit("communication-port", raw.communication_port);
        }
        if (raw.communication_timeout_ms > 0) {
            params.communication.timeout_ms =
                    parse_uint32_or_exit("communication-timeout-ms", raw.communication_timeout_ms);
        }
    }

    void parse_webrtc_parameters(raw_command_line_values &raw, command_line_parameters &params) {
        params.webrtc.signaling_url = take_string(raw.webrtc_signaling_url);
        params.webrtc.stun_server = take_string(raw.webrtc_stun_server);
        params.webrtc.turn_server = take_string(raw.webrtc_turn_server);
        params.webrtc.turn_username = take_string(raw.webrtc_turn_username);
        params.webrtc.turn_password = take_string(raw.webrtc_turn_password);

        if (raw.webrtc_ice_timeout_ms > 0) {
            params.webrtc.ice_timeout_ms = parse_uint32_or_exit("webrtc-ice-timeout-ms", raw.webrtc_ice_timeout_ms);
        }
        if (raw.webrtc_enable_data_channels != FALSE) {
            params.webrtc.enable_data_channels = true;
        }
    }
} // namespace

command_line_parameters parse_command_line(int argc, char **argv) {
    command_line_parameters params{};
    raw_command_line_values raw{};

    const GOptionEntry entries[] = {
            {"config", 'c', G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &raw.config_file,
             "Path to configuration file (required)", nullptr},
            {"common-input-type", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.input_type,
             "Input type: 'file', 'camera', or 'udp'", nullptr},
            {"output-directory", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &raw.output_directory,
             "Root directory for saved original/debug frames", nullptr},
            {"video-file-name", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.file_name,
             "Path to input video file (YUV format)", nullptr},
            {"video-format", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.format,
             "Raw video format (I420, NV12, etc.)", nullptr},
            {"width", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.width, "Video width in pixels", nullptr},
            {"height", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.height, "Video height in pixels", nullptr},
            {"framerate", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.framerate, "Video framerate in fps", nullptr},
            {"background-color", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.background_color,
             "Background color for empty buffers (default: 0)", nullptr},
            {"camera-device", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.camera_device,
             "Camera device path (e.g., /dev/video0)", nullptr},
            {"udp-bind-address", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.udp_bind_address,
             "UDP source bind address (e.g., 0.0.0.0)", nullptr},
            {"udp-port", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.udp_port, "UDP source port to listen on",
             nullptr},
            {"udp-encoding", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.udp_encoding,
             "UDP RTP encoding name (H264 or H265)", nullptr},
            {"udp-payload-type", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.udp_payload_type,
             "UDP RTP payload type (default: 96)", nullptr},
            {"udp-clock-rate", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.udp_clock_rate,
             "UDP RTP clock rate (default: 90000)", nullptr},
            {"udp-latency-ms", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.udp_latency_ms,
             "UDP jitterbuffer latency in milliseconds", nullptr},
            {"deepstream-config-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.deepstream_config_file,
             "Path to DeepStream configuration file", nullptr},
            {"roi-labels", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.roi_labels,
             "Comma-separated list of object labels to detect", nullptr},
            {"roi-data-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.roi_data_file,
             "Path to pre-computed ROI data file", nullptr},
            {"roi-static-json-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.static_roi_json_file,
             "Path to static ROI JSON file", nullptr},
            {"roi-groundtruth-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.groundtruth_roi_file,
             "Path to COCO-format groundtruth JSON file for per-frame ROI detection", nullptr},
            {"roi-inference-engine", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.inference_engine,
             "Enable inference engine for dynamic ROI detection", nullptr},
            {"roi-pack-rois", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.pack_rois,
             "Enable/disable ROI packing. Use 'true' or 'false' (default: from config)", nullptr},
            {"roi-packer-type", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.packer_type,
             "Packer type: 'low_delay' or 'random_access' (default: 'low_delay')", nullptr},
            {"roi-num-frames", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.num_frames,
             "Number of frames to accumulate for low_delay packer (default: 3)", nullptr},
            {"roi-fixed-margin-px", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.roi_fixed_margin_px,
             "Fixed per-side ROI margin in pixels before CTU packing (default: 0)", nullptr},
            {"roi-margin-mode", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.roi_margin_mode,
             "ROI margin mode: 'off', 'content', or 'placement' (default: 'off')", nullptr},
            {"roi-temporal-resampling", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.temporal_resampling,
             "Temporal resampling factor for ROI data", nullptr},
            {"roi-background-mode", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.background_mode,
             "Background ROI mode: 'off', 'background_blur', or 'qp_map_delta' (x86 desktop only)", nullptr},
            {"roi-qp-map-background-delta", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.qp_map_background_delta,
             "QP delta for background-only CTBs in qp_map_delta mode (default: 10)", nullptr},
            {"enable-luma-adaptation", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.enable_luma_adaptation,
             "Enable/disable luma adaptation workflow. Use 'true' or 'false'.", nullptr},
            {"luma-downscale-factor", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &raw.luma_downscale_factor,
             "Luma down-scaling factor applied before encoding (range: 0.0-1.0]", nullptr},
            {"enable-adaptive-luma-adaptation", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
             &raw.enable_adaptive_luma_adaptation,
             "Use input-analyzer NN for adaptive luma (d,u). Use 'true' or 'false'.", nullptr},
            {"input-analyzer-model-path", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &raw.input_analyzer_model_path,
             "Path to ONNX model for adaptive luma input analyzer", nullptr},
            {"adaptive-luma-analyzer-on-keyframes-only", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
             &raw.adaptive_luma_analyzer_on_keyframes_only,
             "Run analyzer only on keyframes (true) or every frame (false). Use 'true' or 'false'.", nullptr},
            {"roi-sigma", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &raw.sigma,
             "Sigma (standard deviation) for Gaussian blur kernel (default: 2.5)", nullptr},
            {"roi-kernel-size", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.kernel_size,
             "Kernel size for Gaussian blur (0 = auto-calculate from sigma) (default: 0)", nullptr},
            {"tracker-config-file", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.tracker_config_file,
             "Path to tracker configuration file", nullptr},
            {"tracker-type", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.tracker_type,
             "Tracker type: IOU, NvSORT, NvDCF, or NvDeepSORT (default: NvSORT)", nullptr},
            {"tracker-max-shadow-tracking-age", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
             &raw.tracker_max_shadow_tracking_age, "Maximum frames to track after detection stops (default: 40)",
             nullptr},
            {"tracker-enable-reassoc", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.tracker_enable_reassoc,
             "Enable trajectory re-association for lost tracks", nullptr},
            {"tracker-trajectory-projection-length", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
             &raw.tracker_trajectory_projection_length, "Trajectory projection length in frames (default: 150)",
             nullptr},
            {"tracker-max-tracklet-matching-time-range", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
             &raw.tracker_max_tracklet_matching_time_range,
             "Maximum tracklet matching time search range in frames (default: 25)", nullptr},
            {"detection-base-confidence-threshold", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE,
             &raw.base_confidence_threshold, "Base confidence threshold for object detection (0.0-1.0)", nullptr},
            {"detection-maximum-missing-frames", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.maximum_missing_frames,
             "Maximum frames to track missing objects", nullptr},
            {"detection-enable-prediction", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.enable_prediction,
             "Enable ROI prediction when tracker stops providing data", nullptr},
            {"stabilization-enabled", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.stabilization_enabled,
             "Enable/disable stabilization (true/false)", nullptr},
            {"stabilization-mode", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.stabilization_mode,
             "Stabilization mode: none, ema, threshold, or hybrid", nullptr},
            {"stabilization-alpha", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &raw.stabilization_alpha,
             "Position smoothing factor (0.0-1.0)", nullptr},
            {"stabilization-alpha-size", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &raw.stabilization_alpha_size,
             "Size smoothing factor (0.0-1.0)", nullptr},
            {"stabilization-min-position-delta", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT,
             &raw.stabilization_min_position_delta, "Minimum pixels to update position", nullptr},
            {"stabilization-min-size-delta", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.stabilization_min_size_delta,
             "Minimum pixels to update size", nullptr},
            {"stabilization-velocity-adaptive-factor", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE,
             &raw.stabilization_velocity_adaptive_factor, "Velocity influence on adaptive alpha", nullptr},
            {"encoder-qp", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.encoder_qp,
             "Constant QP for encoding (0-51, overrides CBR rate control)", nullptr},
            {"encoder-element", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.encoder_element,
             "GStreamer encoder factory name (e.g. nvh265enc, nvv4l2h265enc)", nullptr},
            {"debug-all", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.debug_all,
             "Enable all debug features and output branches", nullptr},
            {"debug-draw-borders", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.draw_borders,
             "Enable border drawing for detected ROIs", nullptr},
            {"debug-draw-rtp-timestamp", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.draw_rtp_timestamp,
             "Enable RTP timestamp overlay on decoded frames", nullptr},
            {"debug-draw-fps", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.draw_fps,
             "Enable FPS overlay on display frames", nullptr},
            {"debug-draw-bitrate", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.draw_bitrate,
             "Enable incoming UDP bitrate overlay on display frames", nullptr},
            {"debug-draw-bitrate-large", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.draw_bitrate_large,
             "Enable large red incoming UDP bitrate overlay on display frames", nullptr},
            {"debug-save-frames", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.save_frames,
             "Enable saving individual frames as PNG files", nullptr},
            {"debug-enable-display-branch", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.enable_display_branch,
             "Enable display branch in the output pipeline", nullptr},
            {"debug-enable-mp4-branch", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.enable_mp4_branch,
             "Enable MP4 file saving branch in the output pipeline", nullptr},
            {"debug-enable-h265-branch", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.enable_h265_branch,
             "Enable H.265 file saving branch in the output pipeline", nullptr},
            {"enable-status-reporter", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.enable_status_reporter,
             "Enable status reporting", nullptr},
            {"logging-packer", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.packer_logging,
             "Enable detailed logging for ROI packing algorithms", nullptr},
            {"communication-transport-type", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING,
             &raw.communication_transport_type, "Communication transport type (rtp, webrtc)", nullptr},
            {"communication-host", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.communication_host,
             "Communication server host address", nullptr},
            {"communication-port", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.communication_port,
             "Communication server port", nullptr},
            {"communication-timeout-ms", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.communication_timeout_ms,
             "Communication timeout in milliseconds", nullptr},
            {"webrtc-signaling-url", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.webrtc_signaling_url,
             "WebRTC signaling server URL (ws://host:port)", nullptr},
            {"webrtc-stun-server", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.webrtc_stun_server,
             "WebRTC STUN server URL", nullptr},
            {"webrtc-turn-server", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.webrtc_turn_server,
             "WebRTC TURN server URL", nullptr},
            {"webrtc-turn-username", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.webrtc_turn_username,
             "WebRTC TURN server username", nullptr},
            {"webrtc-turn-password", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &raw.webrtc_turn_password,
             "WebRTC TURN server password", nullptr},
            {"webrtc-ice-timeout-ms", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &raw.webrtc_ice_timeout_ms,
             "WebRTC ICE candidate timeout in milliseconds", nullptr},
            {"webrtc-enable-data-channels", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &raw.webrtc_enable_data_channels,
             "Enable WebRTC data channels", nullptr},
            {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
    };

    GError *error = nullptr;
    const auto context = std::unique_ptr<GOptionContext, decltype(&g_option_context_free)>(
            g_option_context_new("- Encoder CLI Application with comprehensive command line options"),
            &g_option_context_free);
    g_option_context_add_main_entries(context.get(), entries, nullptr);
    g_option_context_add_group(context.get(), gst_init_get_option_group());

    if (!g_option_context_parse(context.get(), &argc, &argv, &error)) {
        sf::error(nullptr, "Failed to parse command line arguments: %s", error->message);
        g_clear_error(&error);
        exit(1);
    }

    if (raw.config_file == nullptr) {
        sf::error(nullptr, "--config/-c is a required argument. Use --help for details.");
        exit(1);
    }

    params.config_file.reset(raw.config_file);
    raw.config_file = nullptr;

    parse_common_parameters(raw, params);
    parse_video_parameters(raw, params);
    parse_camera_parameters(raw, params);
    parse_udp_parameters(raw, params);
    parse_roi_parameters(raw, params);
    parse_tracker_parameters(raw, params);
    parse_detection_parameters(raw, params);
    parse_stabilization_parameters(raw, params);
    parse_encoder_parameters(raw, params);
    parse_debug_parameters(raw, params);
    parse_logging_parameters(raw, params);
    parse_communication_parameters(raw, params);
    parse_webrtc_parameters(raw, params);

    return params;
}
