// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_config.h"
#include "gst/gpu_elements.h"

#include <algorithm>
#include <cctype>
#include <ranges>

#include "encoder_enums.h"

/**
 * @file encoder_config_validator.cpp
 * @brief Implementation of encoder configuration validation.
 */

namespace {
    /**
     * @brief Returns true when the build target architecture is x86.
     */
    [[nodiscard]] bool is_x86_architecture() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Validates file input parameters.
     * @param config Configuration to validate.
     * @throws std::runtime_error if validation fails.
     */
    void validate_file_input_parameters(const encoder_config &config) {
        // Check required file parameters
        if (!config.file_name || strlen(config.file_name.get()) == 0) {
            throw std::runtime_error("file_name is required when input_type = 'file'");
        }

        if (!config.format.has_value()) {
            throw std::runtime_error("format is required when input_type = 'file'");
        }

        // Check required video properties for file input
        if (config.width <= 0 || config.height <= 0 || config.framerate <= 0) {
            throw std::runtime_error("width, height, and framerate must be positive when input_type = 'file'");
        }
    }

    /**
     * @brief Validates camera input parameters.
     * @param config Configuration to validate.
     * @throws std::runtime_error if validation fails.
     */
    void validate_camera_input_parameters(const encoder_config &config) {
        // Check required camera parameters
        if (config.camera_device.empty()) {
            throw std::runtime_error("camera device is required when input_type = 'camera'");
        }

        if (config.width <= 0 || config.height <= 0) {
            throw std::runtime_error("width and height must be positive when input_type = 'camera'");
        }

        if (config.framerate <= 0) {
            throw std::runtime_error("framerate must be positive when input_type = 'camera'");
        }
    }

    /**
     * @brief Validates UDP input parameters.
     * @param config Configuration to validate.
     * @throws std::runtime_error if validation fails.
     */
    void validate_udp_input_parameters(const encoder_config &config) {
        if (config.input_type_set) {
            std::vector<std::string> missing_keys;
            if (!config.udp.port.has_value()) {
                missing_keys.emplace_back("UDP.port");
            }
            if (!config.udp.encoding.has_value()) {
                missing_keys.emplace_back("UDP.encoding");
            }
            if (!config.udp.payload_type.has_value()) {
                missing_keys.emplace_back("UDP.payload_type");
            }
            if (!config.udp.clock_rate.has_value()) {
                missing_keys.emplace_back("UDP.clock_rate");
            }
            if (!config.udp.latency_ms.has_value()) {
                missing_keys.emplace_back("UDP.latency_ms");
            }
            if (!missing_keys.empty()) {
                std::string message =
                        "UDP configuration is required when input_type is set to 'udp' via command line. Missing:";
                for (const auto &missing_key: missing_keys) {
                    message += " " + missing_key;
                }
                throw std::runtime_error(message);
            }
        }

        if (config.udp.resolved_port() == 0) {
            throw std::runtime_error("udp port must be specified and greater than 0 when input_type = 'udp'");
        }

        if (config.width <= 0 || config.height <= 0) {
            throw std::runtime_error("width and height must be positive when input_type = 'udp'");
        }

        if (config.framerate <= 0) {
            throw std::runtime_error("framerate must be positive when input_type = 'udp'");
        }

        if (config.udp.resolved_payload_type() > 127U) {
            throw std::runtime_error("udp payload_type must be between 0 and 127 when input_type = 'udp'");
        }

        if (config.udp.resolved_clock_rate() == 0U) {
            throw std::runtime_error("udp clock_rate must be greater than 0 when input_type = 'udp'");
        }

        if (config.udp.resolved_encoding().empty()) {
            throw std::runtime_error("udp encoding must be provided when input_type = 'udp'");
        }

        if (config.udp.resolved_latency_ms() < 0) {
            throw std::runtime_error("udp latency_ms must be zero or positive when input_type = 'udp'");
        }

        std::string encoding_upper = config.udp.resolved_encoding();
        std::ranges::transform(encoding_upper, encoding_upper.begin(), toupper);
        if (encoding_upper != "H264" && encoding_upper != "H265") {
            throw std::runtime_error("udp encoding must be either 'H264' or 'H265'");
        }
    }

    /**
     * @brief Validates communication parameters.
     * @param config Configuration to validate.
     * @throws std::runtime_error if validation fails.
     */
    void validate_communication_parameters(const encoder_config &config) {
        if (config.communication.transport_type.has_value()) {
            if (config.communication.host.empty() || config.communication.port == 0) {
                throw std::runtime_error(
                        "Communication host and port must be specified when communication transport type is set");
            }
        }
        if (!config.communication.host.empty() && config.communication.port == 0) {
            throw std::runtime_error("Communication port must be specified when Communication host is provided");
        }
        if (config.communication.host.empty() && config.communication.port != 0) {
            throw std::runtime_error("Communication host must be specified when Communication port is provided");
        }
    }

    /**
     * @brief Validates WebRTC parameters.
     * @param config Configuration to validate.
     * @throws std::runtime_error if validation fails.
     */
    void validate_webrtc_parameters(const encoder_config &config) {
        if (config.communication.resolved_transport_type() == sf::communication::transport_type::webrtc) {
            if (config.webrtc.signaling_url.empty()) {
                throw std::runtime_error("WebRTC signaling_url must be specified when transport_type is 'webrtc'");
            }
        }
    }

    /**
     * @brief Validates ROI processing options.
     * @param config Configuration to validate.
     * @throws std::runtime_error if validation fails.
     */
    void validate_roi_processing_options(const encoder_config &config) {
        // Count how many ROI source modes are active
        const int roi_source_count = (config.inference_engine ? 1 : 0) +
                                     (!config.static_roi_json_file.empty() ? 1 : 0) +
                                     (!config.groundtruth_roi_file.empty() ? 1 : 0);
        if (roi_source_count > 1) {
            throw std::runtime_error(
                    "Only one ROI source may be active at a time. "
                    "inference_engine, static_roi_json_file, and groundtruth_roi_file are mutually exclusive.");
        }

        // If tracker is disabled and inference engine is enabled, roi_accumulation_mode must be "overlap"
        if (config.inference_engine && !config.detection.enable_tracker &&
            config.detection.roi_accumulation_mode_value != roi_accumulation_mode::overlap) {
            throw std::runtime_error(
                    "When enable_tracker=false and inference_engine=true, "
                    "roi_accumulation_mode must be set to 'overlap' (object_id mode requires tracker).");
        }

        if (config.roi_fixed_margin_px < 0) {
            throw std::runtime_error("roi_fixed_margin_px must be non-negative");
        }

        if (config.qp_map_background_delta < -51 || config.qp_map_background_delta > 51) {
            throw std::runtime_error("qp_map_background_delta must be in range [-51, 51]");
        }

        if (config.background_mode_value == background_mode::qp_map_delta && !is_x86_architecture()) {
            throw std::runtime_error("background_mode='qp_map_delta' is supported only on x86 desktop systems; "
                                     "use background_mode='background_blur' or 'off'");
        }

        if (config.background_mode_value != background_mode::off && config.pack_rois) {
            throw std::runtime_error("pack_rois must be disabled when background_mode is not 'off'");
        }

        if (config.luma_downscale_factor <= 0.0 || config.luma_downscale_factor > 1.0) {
            throw std::runtime_error("luma_downscale_factor must be in range (0.0, 1.0]");
        }

        if (config.enable_adaptive_luma_adaptation) {
            if (!config.enable_luma_adaptation) {
                throw std::runtime_error("enable_adaptive_luma_adaptation requires enable_luma_adaptation to be true");
            }
            if (config.input_analyzer_model_path.empty()) {
                throw std::runtime_error(
                        "enable_adaptive_luma_adaptation requires a non-empty input_analyzer_model_path");
            }
        }
    }

    /**
     * @brief Validates stabilization parameters.
     * @param config Configuration to validate.
     * @throws std::runtime_error if validation fails.
     */
    void validate_stabilization_parameters(const encoder_config &config) {
        if (config.stabilization.alpha < 0.0F || config.stabilization.alpha > 1.0F) {
            throw std::runtime_error("stabilization alpha must be between 0.0 and 1.0");
        }
        if (config.stabilization.alpha_size < 0.0F || config.stabilization.alpha_size > 1.0F) {
            throw std::runtime_error("stabilization alpha_size must be between 0.0 and 1.0");
        }
        if (config.stabilization.min_position_delta < 0) {
            throw std::runtime_error("stabilization min_position_delta must be non-negative");
        }
        if (config.stabilization.min_size_delta < 0) {
            throw std::runtime_error("stabilization min_size_delta must be non-negative");
        }
        if (config.stabilization.velocity_adaptive_factor < 0.0F) {
            throw std::runtime_error("stabilization velocity_adaptive_factor must be non-negative");
        }
    }

} // namespace

void validate_configuration(const encoder_config &config) {
    // Validate parameters based on input type
    if (config.input_type_value == input_type::file) {
        validate_file_input_parameters(config);
    } else if (config.input_type_value == input_type::camera) {
        validate_camera_input_parameters(config);
    } else if (config.input_type_value == input_type::udp) {
        validate_udp_input_parameters(config);
    }

    // Validate Communication parameters if provided
    validate_communication_parameters(config);

    // Validate WebRTC parameters if WebRTC transport is selected
    validate_webrtc_parameters(config);

    // Validate ROI processing options
    validate_roi_processing_options(config);

    // Validate stabilization parameters
    validate_stabilization_parameters(config);

    // Encoder element must be registered, supported on this build, and plugin present
    validate_encoder_element_or_throw(config);
}
