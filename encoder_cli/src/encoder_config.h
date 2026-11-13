// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "encoder_enums.h"
#include "gst_raii.h"
#include "libvcai/include/area_copy.h"
#include "libvcai/include/communication/transport.h"
#include "roi/roi_parser.h"

/**
 * @file encoder_config.height
 * @brief Defines the structure and parsing logic for encoder configuration.
 */

/**
 * @brief Initializes the GStreamer debug category for encoder configuration.
 */
void init_encoder_config_debug_category();

/**
 * @brief UDP input configuration values.
 *
 * Optional fields represent values explicitly provided by config or CLI.
 * Runtime defaults are resolved through helper accessors.
 */
struct udp_config {
    std::optional<std::string> bind_address{};
    std::optional<uint16_t> port{};
    std::optional<uint32_t> payload_type{};
    std::optional<uint32_t> clock_rate{};
    std::optional<std::string> encoding{};
    std::optional<int> latency_ms{};

    [[nodiscard]] std::string resolved_bind_address() const { return bind_address.value_or("0.0.0.0"); }

    [[nodiscard]] uint16_t resolved_port() const { return port.value_or(0); }

    [[nodiscard]] uint32_t resolved_payload_type() const { return payload_type.value_or(96U); }

    [[nodiscard]] uint32_t resolved_clock_rate() const { return clock_rate.value_or(90000U); }

    [[nodiscard]] std::string resolved_encoding() const { return encoding.value_or("H264"); }

    [[nodiscard]] int resolved_latency_ms() const { return latency_ms.value_or(200); }
};

/**
 * @brief WebRTC transport-specific configuration.
 */
struct webrtc_config {
    std::string signaling_url{};
    std::string stun_server{};
    std::string turn_server{};
    std::string turn_username{};
    std::string turn_password{};
    uint32_t ice_timeout_ms = 10000U;
    bool enable_data_channels = false;
};

/**
 * @brief Tracker configuration parameters.
 */
struct tracker_config {
    std::string config_file{};
    std::string type = "NvSORT";
    int max_shadow_tracking_age = 40;
    bool enable_reassoc = true;
    int trajectory_projection_length = 150;
    int max_tracklet_matching_time_range = 25;
    int max_targets_per_stream = 300;
    int probation_age = 5;
    int early_termination_age = 1;
    double min_confidence = 0.8049;
    bool use_prediction_for_assoc = true;
};

/**
 * @brief ROI stabilization configuration parameters.
 */
struct encoder_stabilization_config {
    bool enabled = true;
    stabilization_mode mode = stabilization_mode::hybrid;
    float alpha = 0.3F;
    float alpha_size = 0.2F;
    int min_position_delta = 3;
    int min_size_delta = 2;
    float velocity_adaptive_factor = 0.5F;
};

/**
 * @brief Debug drawing and output-branch configuration.
 */
struct debug_config {
    bool draw_borders = false;
    bool draw_rtp_timestamp = false;
    bool draw_fps = false;
    bool draw_bitrate = false;
    bool draw_bitrate_large = false;
    bool save_frames = false;
    bool enable_display_branch = false;
    bool enable_mp4_branch = false;
    bool enable_h265_branch = false;
};

/**
 * @brief Communication transport configuration.
 */
struct communication_config {
    std::optional<sf::communication::transport_type> transport_type{};
    std::string host{};
    uint16_t port = 0;
    uint32_t timeout_ms = 5000U;

    [[nodiscard]] sf::communication::transport_type resolved_transport_type() const {
        return transport_type.value_or(sf::communication::transport_type::udp);
    }
};

/**
 * @brief Performance and bypass tuning configuration.
 */
struct performance_config {
    bool enable_frame_dropping = false;
    double target_fps = 30.0;
    double drop_smoothing_factor = 0.1;
    bool enable_deepstream_bypass = false;
    int bypass_interval = 0;
    size_t merger_buffer_size = 10;
    uint32_t merger_max_latency_ms = 100U;
};

/**
 * @brief ROI detection and tracking-association configuration.
 */
struct detection_config {
    float base_confidence_threshold = 0.5F;
    uint32_t maximum_missing_frames = 25;
    bool enable_prediction = false;
    roi_accumulation_mode roi_accumulation_mode_value = roi_accumulation_mode::object_id;
    float roi_overlap_threshold = 0.33F;
    bool enable_tracker = true;
};

/**
 * @struct encoder_config
 * @brief Holds all configuration parameters for the encoder.
 */
struct encoder_config {
    // --- Common Parameters ---
    // Input type: file, camera, or udp
    input_type input_type_value = input_type::file;
    // True when input_type is explicitly set via CLI.
    bool input_type_set = false;

    // --- Video Parameters (for file input) ---
    // The input file is required when input_type = input_type::file
    sf::unique_g_char_ptr file_name;
    std::optional<sf::video_format> format;

    // --- Camera Parameters (for camera input) ---
    // Camera device path (e.g., /dev/video0) - required when input_type = "camera"
    std::string camera_device{};

    // --- UDP Parameters (for network input) ---
    udp_config udp{};

    // --- Common Video Properties ---
    // Video resolution and framerate - used for all input types
    // These are read from the [Common] section in the config file
    // or set via --width, --height, --framerate CLI options
    int width = 0;
    int height = 0;
    int framerate = 0;
    // Background color for empty buffers (default: 0)
    uint32_t background_color = 0U;
    // Root directory for frame artifacts (debug_frames/original_frames).
    // If not set, falls back to the directory containing encoder_config.ini.
    std::optional<std::string> output_directory{};
    // Absolute parent directory of the loaded encoder_config.ini file.
    std::filesystem::path config_directory{};

    // If --roi-data is provided, this will be populated with detailed info.
    // The --width, --height, and --framerate flags will be ignored if present.
    std::optional<roi_sequence_info> roi_info;
    std::string deepstream_config_file{};
    std::vector<std::string> labels;
    // YOLO model version (e.g., "8n", "11s", "5m")
    // Format: version number + size suffix (n/s/m/l/x)
    // Default: "8n" for YOLOv8 nano
    std::string yolo_version = "8n";
    tracker_config tracker{};

    // --- ROI Processing Options ---
    // Path to static ROI JSON file (mutually exclusive with inference_engine and groundtruth_roi_file)
    std::string static_roi_json_file{};
    /// @brief Path to COCO-format groundtruth JSON file for per-frame ROI detection.
    ///
    /// When set, the encoder uses groundtruth bounding boxes from the COCO JSON as
    /// ROIs instead of running inference or using static ROIs. Each frame's ROIs
    /// are looked up by frame index from the annotations in the file.
    ///
    /// This is mutually exclusive with static_roi_json_file and inference_engine.
    ///
    /// Set via CLI: --roi-groundtruth-file <path>
    /// Set via INI: [ROI] groundtruth_roi_file = <path>
    std::string groundtruth_roi_file{};
    // Enable inference engine for dynamic ROI detection (mutually exclusive with static_roi_json_file)
    bool inference_engine = false;
    // Controls whether ROIs should be packed into a compact layout or kept in original positions
    bool pack_rois = false;
    // Packer type: low_delay or random_access (default: low_delay)
    packer_type packer_type_value = packer_type::low_delay;
    // Number of frames to accumulate for low delay packer (default: 3)
    int num_frames = 3;
    // Fixed per-side ROI margin in pixels applied before CTU packing (default: 0 = disabled)
    int roi_fixed_margin_px = 0;
    // Fixed-margin application mode (default: off)
    roi_margin_mode roi_margin_mode_value = roi_margin_mode::off;
    // Temporal resampling factor for ROI data (default: 1)
    int temporal_resampling = 1;
    // Background processing mode for ROI workflows (default: off)
    background_mode background_mode_value = background_mode::off;
    // Effective runtime mode after capability probing/fallback resolution.
    background_mode effective_background_mode_value = background_mode::off;
    // Runtime reason populated when effective mode differs from requested mode.
    std::string background_mode_fallback_reason{};
    // Runtime CTB size used by QP-map mode. Falls back to legacy default (64).
    int qp_map_ctb_size = 64;
    // QP delta applied to background-only CTBs in qp_map_delta mode.
    int qp_map_background_delta = 10;
    // Enables luma down-scaling before encoding and EOI luma adaptation SEI signaling.
    bool enable_luma_adaptation = false;
    // Multiplicative luma down-scaling factor applied on the Y plane (used when adaptive is off).
    double luma_downscale_factor = 1.0;
    // When true, use input-analyzer NN to predict (d, u) per frame/keyframe; when false, use fixed
    // luma_downscale_factor.
    bool enable_adaptive_luma_adaptation = false;
    // Path to ONNX model for adaptive luma input analyzer (e.g. yolo/model_weights.onnx).
    std::string input_analyzer_model_path{};
    // When true (default), run analyzer only on keyframes and reuse (d,u) for non-keyframes; when false, run every
    // frame.
    bool adaptive_luma_analyzer_on_keyframes_only = true;
    // Sigma (standard deviation) for Gaussian blur kernel (default: 2.5)
    double sigma = 2.5;
    // Kernel size for Gaussian blur (0 = auto-calculate from sigma) (default: 0)
    int kernel_size = 0;

    // --- Detection / Stabilization / Debug ---
    detection_config detection{};
    encoder_stabilization_config stabilization{};
    debug_config debug{};

    // --- Logging Options ---
    // Whether to enable status reporting
    bool enable_status_reporter = false;
    bool enable_performance_monitoring = false;
    int performance_monitoring_interval_seconds = 5;
    // Whether to enable packer logging (for low_delay and random_access packers)
    bool packer_logging = false;

    // --- Communication / WebRTC ---
    communication_config communication{};
    webrtc_config webrtc{};

    // --- Encoder Rate Control ---
    /// @brief Constant QP value for H.265 encoding. Range: 0-51 (lower = higher quality).
    ///
    /// When set, the encoder uses constant-QP mode instead of the default CBR
    /// (Constant Bit Rate) mode. The same QP is applied to I, P, and B frames.
    /// On x86 (nvh265enc): rc-mode=1 (constqp), qp-const-i/p/b are set.
    /// On ARM64 (nvv4l2h265enc): control-rate=0 (VBR), qp-range locks I/P/B min=max
    /// to the configured QP (`MinQpP,MaxQpP:MinQpI,MaxQpI:MinQpB,MaxQpB`). The
    /// plugin has no `quant-range` property; setting that name is ignored.
    ///
    /// When std::nullopt (default), the CBR mode is used with a bitrate computed
    /// from the video resolution and framerate.
    ///
    /// Set via CLI: --encoder-qp <value>
    /// Set via INI: [Encoder] qp = <value>
    std::optional<int> encoder_qp;

    /// @brief GStreamer encoder factory name (optional). When unset, build default is used.
    ///
    /// Must be a registered element (e.g. nvh265enc, nvv4l2h265enc). Unsupported names or
    /// wrong platform cause validation failure at startup.
    ///
    /// Set via CLI: --encoder-element <name>
    /// Set via INI: [Encoder] element = <name>
    std::optional<std::string> encoder_element;

    // --- Performance Options ---
    performance_config performance{};

    [[nodiscard]] bool is_background_blur_requested() const {
        return background_mode_value == background_mode::background_blur;
    }

    [[nodiscard]] bool is_qp_map_delta_requested() const {
        return background_mode_value == background_mode::qp_map_delta;
    }

    [[nodiscard]] bool is_background_blur_effective() const {
        return effective_background_mode_value == background_mode::background_blur;
    }

    [[nodiscard]] bool is_qp_map_delta_effective() const {
        return effective_background_mode_value == background_mode::qp_map_delta;
    }

    [[nodiscard]] bool is_background_processing_enabled() const {
        return effective_background_mode_value != background_mode::off;
    }

    // Default constructor
    encoder_config() :
        input_type_value(input_type::file), input_type_set(false), file_name(), format(std::nullopt), camera_device(),
        udp(), width(0), height(0), framerate(0), background_color(0U), output_directory(std::nullopt),
        config_directory(), roi_info(std::nullopt), deepstream_config_file(), labels(), yolo_version("8n"), tracker(),
        static_roi_json_file(), groundtruth_roi_file(), inference_engine(false), pack_rois(false),
        packer_type_value(packer_type::low_delay), num_frames(3), roi_fixed_margin_px(0),
        roi_margin_mode_value(roi_margin_mode::off), temporal_resampling(1),
        background_mode_value(background_mode::off), effective_background_mode_value(background_mode::off),
        background_mode_fallback_reason(), qp_map_ctb_size(64), qp_map_background_delta(10),
        enable_luma_adaptation(false), luma_downscale_factor(1.0), enable_adaptive_luma_adaptation(false),
        input_analyzer_model_path(), adaptive_luma_analyzer_on_keyframes_only(true), sigma(2.5), kernel_size(0),
        detection(), stabilization(), debug(), enable_status_reporter(false), enable_performance_monitoring(false),
        performance_monitoring_interval_seconds(5), packer_logging(false), communication(), webrtc(),
        encoder_qp(std::nullopt), encoder_element(std::nullopt), performance() {}

    // Move constructor
    encoder_config(encoder_config &&) = default;
    // Move assignment operator
    encoder_config &operator=(encoder_config &&) = default;

    // Delete copy constructor and assignment operator
    encoder_config(const encoder_config &) = delete;
    encoder_config &operator=(const encoder_config &) = delete;
};

/**
 * @brief Resolves the root directory for frame artifact output.
 *
 * Priority:
 * 1. [Output] output_directory (or CLI --output-directory) when set
 * 2. Directory of the loaded encoder_config.ini file
 * 3. Current working directory (defensive fallback)
 *
 * @param config Encoder configuration.
 * @return Root directory for debug/original frame output.
 */
[[nodiscard]] inline std::filesystem::path resolve_frame_output_root_directory(const encoder_config &config) {
    if (config.output_directory.has_value() && !config.output_directory->empty()) {
        return std::filesystem::path(*config.output_directory);
    }
    if (!config.config_directory.empty()) {
        return config.config_directory;
    }
    return std::filesystem::current_path();
}

/**
 * @brief Loads configuration from a file using GLib's KeyFile API.
 *
 * This function reads configuration values from an INI-style file using GLib's
 * KeyFile API. It handles various options for input video properties, ROI data,
 * and detection parameters.
 *
 * @param config_file_path The path to the configuration file.
 * @return A struct containing the loaded configuration.
 * @throws std::runtime_error if the file cannot be loaded or parsed.
 */
[[nodiscard]] encoder_config load_encoder_config(const std::filesystem::path &config_file_path);

/**
 * @brief Returns the intra period for ROI packing and encoder configuration.
 *
 * Uses ROI metadata when available, otherwise falls back to the default value.
 *
 * @param config The encoder configuration to inspect.
 * @return The intra period to use.
 */
[[nodiscard]] inline int get_intra_period(const encoder_config &config) {
    if (config.roi_info && config.roi_info->frame_rate_details) {
        return config.roi_info->frame_rate_details->intra_period;
    }
    return 32;
}

/**
 * @brief Returns true when the configured encoder uses kvazaarenc's intra-period property for GOP sizing.
 */
[[nodiscard]] inline bool encoder_uses_kvazaar_intra_period(const encoder_config &config) {
    const std::string &element = config.encoder_element.value_or("");
    return element == "kvazaarenc" || element == "kvazaar";
}

/**
 * @brief Returns the encoder keyframe period (distance between IRAP frames) in input frame order.
 *
 * Mirrors the GOP / intra-period configured in input_pipeline_encoding so adaptive luma
 * analysis can run on the same cadence as encoder keyframes. kvazaarenc requires the
 * intra-period to be a multiple of its hierarchical B-GOP block length, so a non-aligned
 * value is rounded up to the next aligned period; an unset/illegal period falls back to the
 * B-GOP block length.
 */
[[nodiscard]] inline int get_encoder_keyframe_period(const encoder_config &config) {
    const int gop_size = (config.packer_type_value == packer_type::random_access) ? get_intra_period(config) : 4;
    int effective_gop_size = gop_size > 0 ? gop_size : 1;
    if (encoder_uses_kvazaar_intra_period(config)) {
        constexpr int kvm_b_gop_alignment = 16;
        if (gop_size <= 0) {
            effective_gop_size = kvm_b_gop_alignment;
        } else if (gop_size % kvm_b_gop_alignment != 0) {
            effective_gop_size = ((gop_size + kvm_b_gop_alignment - 1) / kvm_b_gop_alignment) * kvm_b_gop_alignment;
        }
    }
    return effective_gop_size;
}

/**
 * @brief Validates the encoder configuration parameters.
 *
 * This function performs comprehensive validation of all configuration parameters
 * to ensure they are valid and consistent. It checks required parameters based on
 * the input type and validates parameter ranges and relationships.
 *
 * @param config The encoder configuration to validate.
 * @throws std::runtime_error if validation fails.
 */
void validate_configuration(const encoder_config &config);

/**
 * @brief Creates a default configuration file with sample values.
 *
 * This function creates a new configuration file with default values for all
 * parameters. It can be used to generate a template configuration file.
 *
 * @param config_file_path The path where the configuration file should be created.
 * @throws std::runtime_error if the file cannot be created.
 */
void create_default_config_file(const std::filesystem::path &config_file_path);
