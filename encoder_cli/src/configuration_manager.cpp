// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "configuration_manager.h"

#include <chrono>
#include <filesystem>
#include <gst/gst.h>
#include <optional>
#include <sstream>
#include <thread>

#include "../libvcai/include/logging.h"
#include "encoder_config.h"
#include "encoder_enums.h"

/// GStreamer debug category for configuration management.
GST_DEBUG_CATEGORY_STATIC(configuration_manager);

/**
 * @file configuration_manager.cpp
 * @brief Implementation of configuration management and command line parameter overrides.
 */

/**
 * @brief Initializes the GStreamer debug category for configuration manager.
 */
void init_configuration_manager_debug_category() {
    GST_DEBUG_CATEGORY_INIT(configuration_manager, "configuration-manager", 0,
                            "Configuration management and command line parameter overrides");
}


namespace {
    /**
     * @brief Ensures the configuration file exists, creating a default one if needed.
     * @param config_path Path to the configuration file.
     * @throws std::runtime_error if the file cannot be created or accessed.
     */
    void ensure_config_file_exists(const std::filesystem::path &config_path) {
        // Verify config file exists and is readable
        // Retry a few times in case of filesystem timing issues
        bool config_file_exists = false;
        for (int retry = 0; retry < 3; ++retry) {
            if (std::filesystem::exists(config_path)) {
                try {
                    const auto file_size = std::filesystem::file_size(config_path);
                    if (file_size > 0) {
                        config_file_exists = true;
                        break;
                    }
                } catch (const std::filesystem::filesystem_error &) {
                    // File might be in use, retry
                }
            }
            if (retry < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        if (!config_file_exists) {
            sf::warning(configuration_manager,
                        "Configuration file does not exist or is empty: %s. Creating default config file.",
                        config_path.c_str());
            sf::warning(configuration_manager,
                        "Note: Default config may not have all required fields. Ensure config file is created before "
                        "calling this function.");
            create_default_config_file(config_path);

            // Verify the default file was created
            if (!std::filesystem::exists(config_path)) {
                throw std::runtime_error("Failed to create default configuration file: " + config_path.string());
            }

            // Wait a bit to ensure file is written to disk
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Verify config file is readable and has content
        try {
            const auto file_size = std::filesystem::file_size(config_path);
            if (file_size == 0) {
                sf::warning(configuration_manager, "Configuration file is empty: %s", config_path.c_str());
                throw std::runtime_error("Configuration file is empty: " + config_path.string());
            }
            sf::debug(configuration_manager, "Loading configuration from file: %s (size: %zu bytes)",
                      config_path.c_str(), file_size);
        } catch (const std::filesystem::filesystem_error &e) {
            sf::warning(configuration_manager, "Cannot access configuration file: %s - %s", config_path.c_str(),
                        e.what());
            throw std::runtime_error("Cannot access configuration file: " + config_path.string() + " - " + e.what());
        }
    }

    /**
     * @brief Applies common parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_common_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.common.input_type_value.has_value()) {
            config.input_type_value = *params.common.input_type_value;
            config.input_type_set = true;
            sf::info(configuration_manager, "Input type set from command line: %s",
                     input_type_to_string(*params.common.input_type_value));
        }
        if (params.common.output_directory.has_value()) {
            config.output_directory = params.common.output_directory;
            sf::info(configuration_manager, "Output directory set from command line: %s",
                     params.common.output_directory->c_str());
        }
    }

    /**
     * @brief Applies video parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_video_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.video.file_name.has_value()) {
            config.file_name.reset(g_strdup(params.video.file_name->c_str()));
            sf::info(configuration_manager, "Video file name set from command line: %s",
                     params.video.file_name->c_str());
        }
        if (params.video.format.has_value()) {
            config.format = *params.video.format;
            sf::info(configuration_manager, "Video format set from command line: %s",
                     sf::video_format_to_string(*params.video.format));
        }
        if (params.video.width.has_value()) {
            config.width = *params.video.width;
            sf::info(configuration_manager, "Video width set from command line: %d", *params.video.width);
        }
        if (params.video.height.has_value()) {
            config.height = *params.video.height;
            sf::info(configuration_manager, "Video height set from command line: %d", *params.video.height);
        }
        if (params.video.framerate.has_value()) {
            config.framerate = *params.video.framerate;
            sf::info(configuration_manager, "Video framerate set from command line: %d", *params.video.framerate);
        }
        if (params.video.background_color.has_value()) {
            config.background_color = *params.video.background_color;
            sf::info(configuration_manager, "Background color set from command line: %u",
                     *params.video.background_color);
        }

        // For UDP input, set default format if not specified
        if (config.input_type_value == input_type::udp && !params.video.format.has_value() &&
            !config.format.has_value()) {
            config.format = sf::video_format::i420;
            sf::info(configuration_manager, "UDP input raw frames defaulting to I420 for encoder compatibility");
        }
    }

    /**
     * @brief Applies camera parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_camera_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.camera.device.has_value()) {
            config.camera_device = *params.camera.device;
            sf::info(configuration_manager, "Camera device set from command line: %s", params.camera.device->c_str());
        }
    }

    /**
     * @brief Applies UDP parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_udp_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.udp.bind_address.has_value()) {
            config.udp.bind_address = params.udp.bind_address;
            sf::info(configuration_manager, "UDP bind address set from command line: %s",
                     params.udp.bind_address->c_str());
        }
        if (params.udp.port.has_value()) {
            config.udp.port = params.udp.port;
            sf::info(configuration_manager, "UDP port set from command line: %u", *params.udp.port);
        }
        if (params.udp.encoding.has_value()) {
            config.udp.encoding = params.udp.encoding;
            sf::info(configuration_manager, "UDP encoding set from command line: %s", params.udp.encoding->c_str());
        }
        if (params.udp.payload_type.has_value()) {
            config.udp.payload_type = params.udp.payload_type;
            sf::info(configuration_manager, "UDP payload type set from command line: %u", *params.udp.payload_type);
        }
        if (params.udp.clock_rate.has_value()) {
            config.udp.clock_rate = params.udp.clock_rate;
            sf::info(configuration_manager, "UDP clock rate set from command line: %u", *params.udp.clock_rate);
        }
        if (params.udp.latency_ms.has_value()) {
            config.udp.latency_ms = params.udp.latency_ms;
            sf::info(configuration_manager, "UDP latency set from command line: %d ms", *params.udp.latency_ms);
        }
    }

    /**
     * @brief Applies ROI parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_roi_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.roi.deepstream_config_file.has_value()) {
            config.deepstream_config_file = *params.roi.deepstream_config_file;
            sf::info(configuration_manager, "DeepStream config file set from command line: %s",
                     params.roi.deepstream_config_file->c_str());
        }
        if (params.roi.labels.has_value()) {
            // Parse comma-separated labels
            std::stringstream ss(*params.roi.labels);
            std::string label;
            config.labels.clear();
            while (std::getline(ss, label, ',')) {
                // Trim whitespace
                label.erase(0, label.find_first_not_of(" \t"));
                label.erase(label.find_last_not_of(" \t") + 1);
                if (!label.empty()) {
                    config.labels.push_back(label);
                }
            }
            sf::info(configuration_manager, "ROI labels set from command line: %s", params.roi.labels->c_str());
        }
        if (params.roi.data_file.has_value()) {
            // Note: roi_data_file is not directly stored in encoder_config
            // It would need to be added to the struct if needed
            sf::info(configuration_manager, "ROI data file specified from command line: %s",
                     params.roi.data_file->c_str());
        }
        if (params.roi.pack_rois.has_value()) {
            config.pack_rois = *params.roi.pack_rois;
            sf::info(configuration_manager, "ROI pack_rois set from command line: %s",
                     *params.roi.pack_rois ? "true" : "false");
        }
        if (params.roi.temporal_resampling.has_value()) {
            config.temporal_resampling = *params.roi.temporal_resampling;
            sf::info(configuration_manager, "ROI temporal resampling set from command line: %d",
                     *params.roi.temporal_resampling);
        }

        if (params.roi.background_mode_value.has_value()) {
            config.background_mode_value = *params.roi.background_mode_value;
            sf::info(configuration_manager, "ROI background_mode set from command line: %s",
                     background_mode_to_string(*params.roi.background_mode_value));
        }
        if (params.roi.qp_map_background_delta.has_value()) {
            config.qp_map_background_delta = *params.roi.qp_map_background_delta;
            sf::info(configuration_manager, "ROI qp_map_background_delta set from command line: %d",
                     *params.roi.qp_map_background_delta);
        }
        if (params.roi.enable_luma_adaptation.has_value()) {
            config.enable_luma_adaptation = *params.roi.enable_luma_adaptation;
            sf::info(configuration_manager, "ROI enable_luma_adaptation set from command line: %s",
                     *params.roi.enable_luma_adaptation ? "true" : "false");
        }
        if (params.roi.luma_downscale_factor.has_value()) {
            config.luma_downscale_factor = *params.roi.luma_downscale_factor;
            sf::info(configuration_manager, "ROI luma_downscale_factor set from command line: %.3f",
                     *params.roi.luma_downscale_factor);
        }
        if (params.roi.enable_adaptive_luma_adaptation.has_value()) {
            config.enable_adaptive_luma_adaptation = *params.roi.enable_adaptive_luma_adaptation;
            sf::info(configuration_manager, "ROI enable_adaptive_luma_adaptation set from command line: %s",
                     *params.roi.enable_adaptive_luma_adaptation ? "true" : "false");
        }
        if (params.roi.input_analyzer_model_path.has_value()) {
            config.input_analyzer_model_path = *params.roi.input_analyzer_model_path;
            sf::info(configuration_manager, "ROI input_analyzer_model_path set from command line: %s",
                     config.input_analyzer_model_path.c_str());
        }
        if (params.roi.adaptive_luma_analyzer_on_keyframes_only.has_value()) {
            config.adaptive_luma_analyzer_on_keyframes_only = *params.roi.adaptive_luma_analyzer_on_keyframes_only;
            sf::info(configuration_manager, "ROI adaptive_luma_analyzer_on_keyframes_only set from command line: %s",
                     config.adaptive_luma_analyzer_on_keyframes_only ? "true" : "false");
        }
        if (params.roi.sigma.has_value()) {
            config.sigma = *params.roi.sigma;
            sf::info(configuration_manager, "Sigma set from command line: %.2f", *params.roi.sigma);
        }
        if (params.roi.kernel_size.has_value()) {
            config.kernel_size = *params.roi.kernel_size;
            sf::info(configuration_manager, "Kernel size set from command line: %d", *params.roi.kernel_size);
        }

        config.effective_background_mode_value = config.background_mode_value;
        config.background_mode_fallback_reason.clear();

        // Any active background mode disables packing.
        if (config.background_mode_value != background_mode::off) {
            config.pack_rois = false;
            sf::info(configuration_manager, "Background mode '%s' enabled - pack_rois disabled",
                     background_mode_to_string(config.background_mode_value));
        }
        if (params.roi.static_roi_json_file.has_value()) {
            config.static_roi_json_file = *params.roi.static_roi_json_file;
            sf::info(configuration_manager, "Static ROI JSON file set from command line: %s",
                     params.roi.static_roi_json_file->c_str());
        }
        if (params.roi.groundtruth_roi_file.has_value()) {
            config.groundtruth_roi_file = *params.roi.groundtruth_roi_file;
            sf::info(configuration_manager, "Groundtruth ROI file set from command line: %s",
                     params.roi.groundtruth_roi_file->c_str());

            // Groundtruth ROI mode is mutually exclusive with inference and static ROIs.
            // When explicitly set on the command line, override any INI-based settings
            // so the user doesn't have to manually disable them.
            if (config.inference_engine) {
                config.inference_engine = false;
                sf::info(configuration_manager, "Inference engine disabled — overridden by --roi-groundtruth-file");
            }
            if (!config.static_roi_json_file.empty()) {
                config.static_roi_json_file.clear();
                sf::info(configuration_manager, "Static ROI JSON file cleared — overridden by --roi-groundtruth-file");
            }
        }
        if (params.roi.inference_engine.has_value()) {
            config.inference_engine = *params.roi.inference_engine;
            sf::info(configuration_manager, "Inference engine set from command line: %s",
                     *params.roi.inference_engine ? "true" : "false");
        }
        if (params.roi.packer_type_value.has_value()) {
            config.packer_type_value = *params.roi.packer_type_value;
            sf::info(configuration_manager, "Packer type set from command line: %s",
                     packer_type_to_string(*params.roi.packer_type_value));
        }
        if (params.roi.num_frames.has_value()) {
            config.num_frames = *params.roi.num_frames;
            sf::info(configuration_manager, "Num frames set from command line: %d", *params.roi.num_frames);
        }
        if (params.roi.roi_fixed_margin_px.has_value()) {
            config.roi_fixed_margin_px = *params.roi.roi_fixed_margin_px;
            sf::info(configuration_manager, "ROI fixed margin set from command line: %d px",
                     *params.roi.roi_fixed_margin_px);
        }
        if (params.roi.roi_margin_mode_value.has_value()) {
            config.roi_margin_mode_value = *params.roi.roi_margin_mode_value;
            sf::info(configuration_manager, "ROI margin mode set from command line: %s",
                     roi_margin_mode_to_string(*params.roi.roi_margin_mode_value));
        }
    }

    /**
     * @brief Applies tracker parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_tracker_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.tracker.config_file.has_value()) {
            config.tracker.config_file = *params.tracker.config_file;
            sf::info(configuration_manager, "Tracker config file set from command line: %s",
                     params.tracker.config_file->c_str());
        }
        if (params.tracker.type.has_value()) {
            config.tracker.type = *params.tracker.type;
            sf::info(configuration_manager, "Tracker type set from command line: %s", params.tracker.type->c_str());
        }
        if (params.tracker.max_shadow_tracking_age.has_value()) {
            config.tracker.max_shadow_tracking_age = *params.tracker.max_shadow_tracking_age;
            sf::info(configuration_manager, "Tracker max shadow tracking age set from command line: %d",
                     *params.tracker.max_shadow_tracking_age);
        }
        if (params.tracker.enable_reassoc.has_value()) {
            config.tracker.enable_reassoc = *params.tracker.enable_reassoc;
            sf::info(configuration_manager, "Tracker enable reassoc set from command line: %s",
                     *params.tracker.enable_reassoc ? "true" : "false");
        }
        if (params.tracker.trajectory_projection_length.has_value()) {
            config.tracker.trajectory_projection_length = *params.tracker.trajectory_projection_length;
            sf::info(configuration_manager, "Tracker trajectory projection length set from command line: %d",
                     *params.tracker.trajectory_projection_length);
        }
        if (params.tracker.max_tracklet_matching_time_range.has_value()) {
            config.tracker.max_tracklet_matching_time_range = *params.tracker.max_tracklet_matching_time_range;
            sf::info(configuration_manager, "Tracker max tracklet matching time range set from command line: %d",
                     *params.tracker.max_tracklet_matching_time_range);
        }
    }

    /**
     * @brief Applies detection parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_detection_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.detection.base_confidence_threshold.has_value()) {
            config.detection.base_confidence_threshold = *params.detection.base_confidence_threshold;
            sf::info(configuration_manager, "Detection base confidence threshold set from command line: %.3f",
                     static_cast<double>(*params.detection.base_confidence_threshold));
        }
        if (params.detection.maximum_missing_frames.has_value()) {
            config.detection.maximum_missing_frames = static_cast<uint32_t>(*params.detection.maximum_missing_frames);
            sf::info(configuration_manager, "Detection maximum missing frames set from command line: %d",
                     *params.detection.maximum_missing_frames);
        }
        if (params.detection.enable_prediction.has_value()) {
            config.detection.enable_prediction = *params.detection.enable_prediction;
            sf::info(configuration_manager, "Detection enable prediction set from command line: %s",
                     *params.detection.enable_prediction ? "true" : "false");
        }
    }

    /**
     * @brief Applies stabilization parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_stabilization_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.stabilization.enabled.has_value()) {
            config.stabilization.enabled = *params.stabilization.enabled;
            sf::info(configuration_manager, "Stabilization enabled set from command line: %s",
                     *params.stabilization.enabled ? "true" : "false");
        }
        if (params.stabilization.mode.has_value()) {
            config.stabilization.mode = *params.stabilization.mode;
            sf::info(configuration_manager, "Stabilization mode set from command line: %s",
                     stabilization_mode_to_string(*params.stabilization.mode));
        }
        if (params.stabilization.alpha.has_value()) {
            config.stabilization.alpha = *params.stabilization.alpha;
            sf::info(configuration_manager, "Stabilization alpha set from command line: %.3f",
                     static_cast<double>(*params.stabilization.alpha));
        }
        if (params.stabilization.alpha_size.has_value()) {
            config.stabilization.alpha_size = *params.stabilization.alpha_size;
            sf::info(configuration_manager, "Stabilization alpha_size set from command line: %.3f",
                     static_cast<double>(*params.stabilization.alpha_size));
        }
        if (params.stabilization.min_position_delta.has_value()) {
            config.stabilization.min_position_delta = *params.stabilization.min_position_delta;
            sf::info(configuration_manager, "Stabilization min_position_delta set from command line: %d",
                     *params.stabilization.min_position_delta);
        }
        if (params.stabilization.min_size_delta.has_value()) {
            config.stabilization.min_size_delta = *params.stabilization.min_size_delta;
            sf::info(configuration_manager, "Stabilization min_size_delta set from command line: %d",
                     *params.stabilization.min_size_delta);
        }
        if (params.stabilization.velocity_adaptive_factor.has_value()) {
            config.stabilization.velocity_adaptive_factor = *params.stabilization.velocity_adaptive_factor;
            sf::info(configuration_manager, "Stabilization velocity_adaptive_factor set from command line: %.3f",
                     static_cast<double>(*params.stabilization.velocity_adaptive_factor));
        }
    }

    /**
     * @brief Applies debug parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_debug_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.debug.debug_all) {
            // --debug-all overrides all debug settings
            config.debug.draw_borders = true;
            config.debug.draw_rtp_timestamp = true;
            config.debug.draw_fps = true;
            config.debug.save_frames = true;
            config.packer_logging = true;
            config.debug.enable_display_branch = true;
            config.debug.enable_mp4_branch = true;
            config.debug.enable_h265_branch = true;
            sf::info(configuration_manager, "Debug all enabled - all debug features and output branches activated");
        } else {
            // Only override individual debug parameters if --debug-all is not specified
            if (params.debug.draw_borders.has_value()) {
                config.debug.draw_borders = *params.debug.draw_borders;
                sf::info(configuration_manager, "Debug draw borders set from command line: %s",
                         *params.debug.draw_borders ? "true" : "false");
            }
            if (params.debug.draw_rtp_timestamp.has_value()) {
                config.debug.draw_rtp_timestamp = *params.debug.draw_rtp_timestamp;
                sf::info(configuration_manager, "Debug draw RTP timestamp set from command line: %s",
                         *params.debug.draw_rtp_timestamp ? "true" : "false");
            }
            if (params.debug.draw_fps.has_value()) {
                config.debug.draw_fps = *params.debug.draw_fps;
                sf::info(configuration_manager, "Debug draw FPS set from command line: %s",
                         *params.debug.draw_fps ? "true" : "false");
            }
            if (params.debug.draw_bitrate.has_value()) {
                config.debug.draw_bitrate = *params.debug.draw_bitrate;
                sf::info(configuration_manager, "Debug draw bitrate set from command line: %s",
                         *params.debug.draw_bitrate ? "true" : "false");
            }
            if (params.debug.draw_bitrate_large.has_value()) {
                config.debug.draw_bitrate_large = *params.debug.draw_bitrate_large;
                sf::info(configuration_manager, "Debug draw bitrate large set from command line: %s",
                         *params.debug.draw_bitrate_large ? "true" : "false");
            }
            if (params.debug.save_frames.has_value()) {
                config.debug.save_frames = *params.debug.save_frames;
                sf::info(configuration_manager, "Debug save frames set from command line: %s",
                         *params.debug.save_frames ? "true" : "false");
            }
            if (params.debug.enable_display_branch.has_value()) {
                config.debug.enable_display_branch = *params.debug.enable_display_branch;
                sf::info(configuration_manager, "Debug enable display branch set from command line: %s",
                         *params.debug.enable_display_branch ? "true" : "false");
            }
            if (params.debug.enable_mp4_branch.has_value()) {
                config.debug.enable_mp4_branch = *params.debug.enable_mp4_branch;
                sf::info(configuration_manager, "Debug enable MP4 branch set from command line: %s",
                         *params.debug.enable_mp4_branch ? "true" : "false");
            }
            if (params.debug.enable_h265_branch.has_value()) {
                config.debug.enable_h265_branch = *params.debug.enable_h265_branch;
                sf::info(configuration_manager, "Debug enable H.265 branch set from command line: %s",
                         *params.debug.enable_h265_branch ? "true" : "false");
            }
        }
    }

    /**
     * @brief Applies logging parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_logging_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.logging.enable_status_reporter.has_value()) {
            config.enable_status_reporter = *params.logging.enable_status_reporter;
            sf::info(configuration_manager, "Status reporter set from command line: %s",
                     *params.logging.enable_status_reporter ? "true" : "false");
        }
        if (params.logging.packer_logging.has_value()) {
            config.packer_logging = *params.logging.packer_logging;
            sf::info(configuration_manager, "Packer logging set from command line: %s",
                     *params.logging.packer_logging ? "true" : "false");
        }
    }

    /**
     * @brief Applies encoder rate control parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_encoder_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.encoder.qp.has_value()) {
            config.encoder_qp = params.encoder.qp;
            sf::info(configuration_manager, "Encoder QP set from command line: %d (constant-QP mode)",
                     *params.encoder.qp);
        }
        if (params.encoder.element.has_value()) {
            config.encoder_element = *params.encoder.element;
            sf::info(configuration_manager, "Encoder element set from command line: %s",
                     params.encoder.element->c_str());
        }
    }

    /**
     * @brief Applies performance parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_performance_parameter_overrides([[maybe_unused]] encoder_config &config,
                                               [[maybe_unused]] const command_line_parameters &params) {}

    /**
     * @brief Applies communication parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_communication_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.communication.transport_type.has_value()) {
            config.communication.transport_type = *params.communication.transport_type;
            const char *transport_str =
                    (*params.communication.transport_type == sf::communication::transport_type::udp) ? "rtp" : "webrtc";
            sf::info(configuration_manager, "Communication transport_type set from command line: %s", transport_str);
        }
        if (params.communication.host.has_value()) {
            config.communication.host = *params.communication.host;
            sf::info(configuration_manager, "Communication host set from command line: %s",
                     params.communication.host->c_str());
        }
        if (params.communication.port.has_value()) {
            config.communication.port = *params.communication.port;
            sf::info(configuration_manager, "Communication port set from command line: %u", *params.communication.port);
        }
        if (params.communication.timeout_ms.has_value()) {
            config.communication.timeout_ms = *params.communication.timeout_ms;
            sf::info(configuration_manager, "Communication timeout_ms set from command line: %u",
                     *params.communication.timeout_ms);
        }
    }

    /**
     * @brief Applies WebRTC parameter overrides from command line.
     * @param config Configuration to modify.
     * @param params Command line parameters.
     */
    void apply_webrtc_parameter_overrides(encoder_config &config, const command_line_parameters &params) {
        if (params.webrtc.signaling_url.has_value()) {
            config.webrtc.signaling_url = *params.webrtc.signaling_url;
            sf::info(configuration_manager, "WebRTC signaling_url set from command line: %s",
                     params.webrtc.signaling_url->c_str());
        }
        if (params.webrtc.stun_server.has_value()) {
            config.webrtc.stun_server = *params.webrtc.stun_server;
            sf::info(configuration_manager, "WebRTC stun_server set from command line: %s",
                     params.webrtc.stun_server->c_str());
        }
        if (params.webrtc.turn_server.has_value()) {
            config.webrtc.turn_server = *params.webrtc.turn_server;
            sf::info(configuration_manager, "WebRTC turn_server set from command line: %s",
                     params.webrtc.turn_server->c_str());
        }
        if (params.webrtc.turn_username.has_value()) {
            config.webrtc.turn_username = *params.webrtc.turn_username;
        }
        if (params.webrtc.turn_password.has_value()) {
            config.webrtc.turn_password = *params.webrtc.turn_password;
        }
        if (params.webrtc.ice_timeout_ms.has_value()) {
            config.webrtc.ice_timeout_ms = *params.webrtc.ice_timeout_ms;
            sf::info(configuration_manager, "WebRTC ice_timeout_ms set from command line: %u",
                     *params.webrtc.ice_timeout_ms);
        }
        if (params.webrtc.enable_data_channels.has_value()) {
            config.webrtc.enable_data_channels = *params.webrtc.enable_data_channels;
            sf::info(configuration_manager, "WebRTC enable_data_channels set from command line: %s",
                     *params.webrtc.enable_data_channels ? "true" : "false");
        }
    }
} // namespace

/**
 * @brief Loads configuration and applies command-line overrides.
 * @param params Parsed command-line parameters.
 * @return Fully configured encoder configuration.
 */
encoder_config load_and_configure_encoder(const command_line_parameters &params) {
    const std::filesystem::path config_path(params.config_file.get());
    ensure_config_file_exists(config_path);

    auto config = load_encoder_config(config_path);
    config.config_directory = std::filesystem::absolute(config_path).parent_path();

    // Log what was loaded for debugging
    sf::debug(configuration_manager, "Config loaded - input_type: %s, file_name: %s",
              input_type_to_string(config.input_type_value), config.file_name ? config.file_name.get() : "(null)");

    // Apply command line parameter overrides
    apply_common_parameter_overrides(config, params);
    apply_video_parameter_overrides(config, params);
    apply_camera_parameter_overrides(config, params);
    apply_udp_parameter_overrides(config, params);
    apply_roi_parameter_overrides(config, params);
    apply_tracker_parameter_overrides(config, params);
    apply_detection_parameter_overrides(config, params);
    apply_stabilization_parameter_overrides(config, params);
    apply_encoder_parameter_overrides(config, params);
    apply_debug_parameter_overrides(config, params);
    apply_logging_parameter_overrides(config, params);
    apply_performance_parameter_overrides(config, params);
    apply_communication_parameter_overrides(config, params);
    apply_webrtc_parameter_overrides(config, params);

    sf::info(configuration_manager, "Final Communication configuration - Transport: '%s', Host: '%s', Port: %u",
             (config.communication.resolved_transport_type() == sf::communication::transport_type::webrtc ? "webrtc"
                                                                                                          : "rtp"),
             config.communication.host.c_str(), config.communication.port);

    sf::info(configuration_manager,
             "Final ROI configuration - background_mode: '%s' (effective '%s'), pack_rois: %s, "
             "inference_engine: %s, packer_type: '%s', roi_fixed_margin_px: %d, roi_margin_mode: '%s', "
             "enable_luma_adaptation: %s, enable_adaptive_luma_adaptation: %s, luma_downscale_factor: %.3f",
             background_mode_to_string(config.background_mode_value),
             background_mode_to_string(config.effective_background_mode_value), config.pack_rois ? "true" : "false",
             config.inference_engine ? "true" : "false", packer_type_to_string(config.packer_type_value),
             config.roi_fixed_margin_px, roi_margin_mode_to_string(config.roi_margin_mode_value),
             config.enable_luma_adaptation ? "true" : "false",
             config.enable_adaptive_luma_adaptation ? "true" : "false", config.luma_downscale_factor);

    // Validate the complete configuration after applying command line overrides
    validate_configuration(config);

    return config;
}
