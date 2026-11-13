// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_config.h"

#include <gst/gst.h>

#include "encoder_enums.h"
#include "gst_raii.h"

/**
 * @file encoder_config_defaults.cpp
 * @brief Implementation of default encoder configuration file generation.
 */

/**
 * @brief Creates a default configuration file with sample values.
 *
 * This function creates a new configuration file with default values for all
 * parameters. It can be used to generate a template configuration file.
 *
 * @param config_file_path The path where the configuration file should be created.
 * @throws std::runtime_error if the file cannot be created.
 */
void create_default_config_file(const std::filesystem::path &config_file_path) {
    const sf::unique_g_key_file_ptr key_file(g_key_file_new());

    // Common section
    g_key_file_set_string(key_file.get(), "Common", "input_type", "file");
    g_key_file_set_int64(key_file.get(), "Common", "background_color", 0);

    // Output section
    g_key_file_set_string(key_file.get(), "Output", "output_directory", "");

    // Video section
    g_key_file_set_string(key_file.get(), "Video", "file_name", "input.yuv");
    g_key_file_set_string(key_file.get(), "Video", "format", "I420");
    g_key_file_set_integer(key_file.get(), "Video", "width", 1920);
    g_key_file_set_integer(key_file.get(), "Video", "height", 1080);
    g_key_file_set_integer(key_file.get(), "Video", "framerate", 30);

    // Camera section
    g_key_file_set_string(key_file.get(), "Camera", "device", "/dev/video0");
    g_key_file_set_integer(key_file.get(), "Camera", "width", 1920);
    g_key_file_set_integer(key_file.get(), "Camera", "height", 1080);
    g_key_file_set_integer(key_file.get(), "Camera", "framerate", 25);

    // ROI section
    g_key_file_set_string(key_file.get(), "ROI", "deepstream_config_file", "");
    g_key_file_set_string(key_file.get(), "ROI", "labels", "person,car");
    g_key_file_set_string(key_file.get(), "ROI", "yolo_version", "8n");
    g_key_file_set_string(key_file.get(), "ROI", "data_file", "");
    g_key_file_set_boolean(key_file.get(), "ROI", "pack_rois", true);
    g_key_file_set_string(key_file.get(), "ROI", "packer_type", packer_type_to_string(packer_type::low_delay));
    g_key_file_set_integer(key_file.get(), "ROI", "num_frames", 3);
    g_key_file_set_integer(key_file.get(), "ROI", "roi_fixed_margin_px", 0);
    g_key_file_set_string(key_file.get(), "ROI", "roi_margin_mode", roi_margin_mode_to_string(roi_margin_mode::off));
    g_key_file_set_integer(key_file.get(), "ROI", "temporal_resampling", 1);
    g_key_file_set_string(key_file.get(), "ROI", "background_mode", background_mode_to_string(background_mode::off));
    g_key_file_set_integer(key_file.get(), "ROI", "qp_map_background_delta", 10);
    g_key_file_set_boolean(key_file.get(), "ROI", "enable_luma_adaptation", false);
    g_key_file_set_double(key_file.get(), "ROI", "luma_downscale_factor", 1.0);
    g_key_file_set_boolean(key_file.get(), "ROI", "enable_adaptive_luma_adaptation", false);
    g_key_file_set_string(key_file.get(), "ROI", "input_analyzer_model_path", "");
    g_key_file_set_boolean(key_file.get(), "ROI", "adaptive_luma_analyzer_on_keyframes_only", true);
    g_key_file_set_double(key_file.get(), "ROI", "sigma", 2.5);
    g_key_file_set_integer(key_file.get(), "ROI", "kernel_size", 0);

    // Tracker section
    g_key_file_set_string(key_file.get(), "Tracker", "tracker_config_file", "");
    g_key_file_set_string(key_file.get(), "Tracker", "tracker_type", "NvSORT");
    // Tracker defaults tuned to reduce latency and CPU/GPU load on weaker systems.
    g_key_file_set_integer(key_file.get(), "Tracker", "tracker_max_shadow_tracking_age",
                           10); // Lower value limits stale tracks
    g_key_file_set_boolean(key_file.get(), "Tracker", "tracker_enable_reassoc",
                           false); // Disable reassociation to reduce compute
    g_key_file_set_integer(key_file.get(), "Tracker", "tracker_trajectory_projection_length",
                           50); // Shorter projection window
    g_key_file_set_integer(key_file.get(), "Tracker", "tracker_max_tracklet_matching_time_range",
                           10); // Narrow matching window
    g_key_file_set_integer(key_file.get(), "Tracker", "tracker_max_targets_per_stream",
                           100); // Fewer concurrent targets reduces load
    g_key_file_set_integer(key_file.get(), "Tracker", "tracker_probation_age", 3); // Faster track confirmation
    g_key_file_set_integer(key_file.get(), "Tracker", "tracker_early_termination_age",
                           2); // End short-lived tracks sooner
    g_key_file_set_double(key_file.get(), "Tracker", "tracker_min_confidence", 0.85); // Filter weak tracks earlier
    g_key_file_set_boolean(key_file.get(), "Tracker", "tracker_use_prediction_for_assoc",
                           false); // Skip prediction to reduce computation

    // Detection section
    g_key_file_set_double(key_file.get(), "Detection", "base_confidence_threshold", 0.5);
    g_key_file_set_integer(key_file.get(), "Detection", "maximum_missing_frames", 25);
    g_key_file_set_boolean(key_file.get(), "Detection", "enable_prediction", false);
    g_key_file_set_string(key_file.get(), "Detection", "roi_accumulation_mode",
                          roi_accumulation_mode_to_string(roi_accumulation_mode::object_id));
    g_key_file_set_double(key_file.get(), "Detection", "roi_overlap_threshold", 0.33);
    g_key_file_set_boolean(key_file.get(), "Detection", "enable_tracker", true);

    // Stabilization section
    g_key_file_set_boolean(key_file.get(), "Stabilization", "enabled", true);
    g_key_file_set_string(key_file.get(), "Stabilization", "mode",
                          stabilization_mode_to_string(stabilization_mode::hybrid));
    g_key_file_set_double(key_file.get(), "Stabilization", "alpha", 0.3);
    g_key_file_set_double(key_file.get(), "Stabilization", "alpha_size", 0.2);
    g_key_file_set_integer(key_file.get(), "Stabilization", "min_position_delta", 3);
    g_key_file_set_integer(key_file.get(), "Stabilization", "min_size_delta", 2);
    g_key_file_set_double(key_file.get(), "Stabilization", "velocity_adaptive_factor", 0.5);

    // Encoder section (optional qp / element; omit element to use build default nvh265enc or nvv4l2h265enc)
    // g_key_file_set_string(key_file.get(), "Encoder", "element", "nvh265enc");

    // Debug section
    g_key_file_set_boolean(key_file.get(), "Debug", "draw_borders", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "draw_bitrate", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "draw_bitrate_large", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "draw_rtp_timestamp", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "draw_fps", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "save_frames", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "enable_display_branch", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "enable_mp4_branch", false);
    g_key_file_set_boolean(key_file.get(), "Debug", "enable_h265_branch", false);

    // Logging section
    g_key_file_set_boolean(key_file.get(), "Logging", "enable_status_reporter", true);
    g_key_file_set_boolean(key_file.get(), "Logging", "enable_performance_monitoring", false);
    g_key_file_set_integer(key_file.get(), "Logging", "performance_monitoring_interval_seconds", 5);
    g_key_file_set_boolean(key_file.get(), "Logging", "packer_logging", false);

    // Performance section
    g_key_file_set_boolean(key_file.get(), "Performance", "enable_frame_dropping", false);
    g_key_file_set_double(key_file.get(), "Performance", "target_fps", 30.0);
    g_key_file_set_double(key_file.get(), "Performance", "drop_smoothing_factor", 0.1);
    g_key_file_set_boolean(key_file.get(), "Performance", "enable_deepstream_bypass", false);
    g_key_file_set_integer(key_file.get(), "Performance", "bypass_interval", 0);
    g_key_file_set_integer(key_file.get(), "Performance", "merger_buffer_size", 10);
    g_key_file_set_integer(key_file.get(), "Performance", "merger_max_latency_ms", 100);

    // UDP input section
    g_key_file_set_string(key_file.get(), "UDP", "bind_address", "0.0.0.0");
    g_key_file_set_integer(key_file.get(), "UDP", "port", 5000);
    g_key_file_set_integer(key_file.get(), "UDP", "payload_type", 96);
    g_key_file_set_integer(key_file.get(), "UDP", "clock_rate", 90000);
    g_key_file_set_integer(key_file.get(), "UDP", "latency_ms", 200);
    g_key_file_set_string(key_file.get(), "UDP", "encoding", "H264");
    g_key_file_set_integer(key_file.get(), "UDP", "width", 1920);
    g_key_file_set_integer(key_file.get(), "UDP", "height", 1080);
    g_key_file_set_integer(key_file.get(), "UDP", "framerate", 30);

    // Communication section
    g_key_file_set_string(key_file.get(), "Communication", "transport_type", "rtp");
    g_key_file_set_string(key_file.get(), "Communication", "host", "");
    g_key_file_set_integer(key_file.get(), "Communication", "port", 0);
    g_key_file_set_integer(key_file.get(), "Communication", "timeout_ms", 5000);

    // WebRTC section
    g_key_file_set_string(key_file.get(), "WebRTC", "signaling_url", "");
    g_key_file_set_string(key_file.get(), "WebRTC", "stun_server", "");
    g_key_file_set_string(key_file.get(), "WebRTC", "turn_server", "");
    g_key_file_set_string(key_file.get(), "WebRTC", "turn_username", "");
    g_key_file_set_string(key_file.get(), "WebRTC", "turn_password", "");
    g_key_file_set_integer(key_file.get(), "WebRTC", "ice_timeout_ms", 10000);
    g_key_file_set_boolean(key_file.get(), "WebRTC", "enable_data_channels", false);

    // Write the configuration file
    gsize length;
    const sf::unique_g_char_ptr data(g_key_file_to_data(key_file.get(), &length, nullptr));

    GError *error = nullptr;
    if (!g_file_set_contents(config_file_path.c_str(), data.get(), static_cast<gssize>(length), &error)) {
        const std::string error_msg = error ? error->message : "Unknown error";
        g_clear_error(&error);
        throw std::runtime_error(std::string("Failed to write configuration file: ") + error_msg);
    }
}
