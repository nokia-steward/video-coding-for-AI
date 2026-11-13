// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "integration_test_helpers.h"
#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../../encoder_cli/src/encoder_cli_debug.h"
#include "../../encoder_cli/src/gst/input_pipeline.h"
#include "../../encoder_cli/src/gst/input_types/input_ingress_facade.h"
#include "../../encoder_cli/src/gst/input_types/udp_input.h"
#include "../../encoder_cli/src/gst/output_pipeline.h"
#include "test_debug_categories.h"

#include <chrono>
#include <fstream>
#include <gst/gst.h>
#include <sstream>
#include <thread>
#include <tuple>
#include <vector>

bool create_test_video(const std::filesystem::path &output_path, int width, int height, int framerate,
                       int duration_seconds, const std::string &format) {
    // Ensure output directory exists
    const auto parent_dir = output_path.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        std::filesystem::create_directories(parent_dir);
    }

    // Calculate number of frames
    const int num_buffers = duration_seconds * framerate;

    // Build GStreamer pipeline command
    std::ostringstream pipeline_cmd;

    if (format == "mp4") {
        // MP4 format: videotestsrc -> x264enc -> mp4mux -> filesink
        pipeline_cmd << "gst-launch-1.0 -q "
                     << "videotestsrc pattern=smpte num-buffers=" << num_buffers << " "
                     << "! video/x-raw,width=" << width << ",height=" << height << ",framerate=" << framerate << "/1 "
                     << "! x264enc tune=zerolatency speed-preset=ultrafast "
                     << "! mp4mux "
                     << "! filesink location=" << output_path.string();
    } else if (format == "h264") {
        // Raw H.264 format: videotestsrc -> x264enc -> filesink
        pipeline_cmd << "gst-launch-1.0 -q "
                     << "videotestsrc pattern=smpte num-buffers=" << num_buffers << " "
                     << "! video/x-raw,width=" << width << ",height=" << height << ",framerate=" << framerate << "/1 "
                     << "! x264enc tune=zerolatency speed-preset=ultrafast "
                     << "! filesink location=" << output_path.string();
    } else if (format == "h265") {
        // H.265 format: videotestsrc -> x265enc -> filesink
        pipeline_cmd << "gst-launch-1.0 -q "
                     << "videotestsrc pattern=smpte num-buffers=" << num_buffers << " "
                     << "! video/x-raw,width=" << width << ",height=" << height << ",framerate=" << framerate << "/1 "
                     << "! x265enc tune=zerolatency speed-preset=ultrafast "
                     << "! filesink location=" << output_path.string();
    } else {
        // Default: raw YUV format
        pipeline_cmd << "gst-launch-1.0 -q "
                     << "videotestsrc pattern=smpte num-buffers=" << num_buffers << " "
                     << "! video/x-raw,width=" << width << ",height=" << height << ",framerate=" << framerate
                     << "/1,format=I420 "
                     << "! filesink location=" << output_path.string();
    }

    // Execute pipeline
    const int result = system(pipeline_cmd.str().c_str());

    // Verify file was created and has content
    if (result == 0 && std::filesystem::exists(output_path)) {
        const auto file_size = std::filesystem::file_size(output_path);
        return file_size > 0;
    }

    return false;
}

bool create_test_config(const std::filesystem::path &config_path, const std::filesystem::path &video_file_path,
                        int width, int height, int framerate, const std::optional<std::filesystem::path> &output_file) {
    // Ensure output directory exists
    const auto parent_dir = config_path.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        std::filesystem::create_directories(parent_dir);
    }

    std::ofstream config_file(config_path);
    if (!config_file.is_open()) {
        return false;
    }

    // Write config in the format expected by encoder_config.cpp
    // [Common] section
    config_file << "[Common]\n";
    config_file << "input_type=file\n";
    config_file << "width=" << width << "\n";
    config_file << "height=" << height << "\n";
    config_file << "framerate=" << framerate << "\n";

    // [Video] section
    config_file << "\n[Video]\n";
    config_file << "file_name=" << video_file_path.string() << "\n";
    config_file << "format=I420\n";

    // [ROI] section
    config_file << "\n[ROI]\n";
    config_file << "pack_rois=false\n";
    config_file << "inference_engine=false\n";

    // [Debug] section
    config_file << "\n[Debug]\n";
    config_file << "draw_borders=false\n";
    config_file << "save_frames=false\n";
    config_file << "enable_display_branch=false\n";
    if (output_file.has_value()) {
        config_file << "enable_h265_branch=true\n";
    } else {
        config_file << "enable_h265_branch=false\n";
    }

    config_file.flush();
    config_file.close();

    // Force filesystem sync to ensure file is written to disk
    // This helps prevent race conditions when tests run in quick succession
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Verify file was written correctly
    if (!std::filesystem::exists(config_path)) {
        return false;
    }

    // Verify file has content
    const auto file_size = std::filesystem::file_size(config_path);
    if (file_size == 0) {
        return false;
    }

    // Verify file_name is in the file (basic sanity check)
    std::ifstream verify_file(config_path);
    if (verify_file.is_open()) {
        std::string line;
        bool found_file_name = false;
        while (std::getline(verify_file, line)) {
            if (line.find("file_name=") != std::string::npos) {
                found_file_name = true;
                break;
            }
        }
        verify_file.close();
        if (!found_file_name) {
            return false;
        }
    }

    return true;
}

bool verify_output_file(const std::filesystem::path &file_path) {
    if (!std::filesystem::exists(file_path)) {
        return false;
    }

    const auto file_size = std::filesystem::file_size(file_path);
    return file_size > 0;
}

std::filesystem::path get_test_data_dir() {
    const auto test_dir = std::filesystem::path(__FILE__).parent_path();
    return test_dir / "test_data";
}

std::filesystem::path get_test_output_dir() {
    const auto test_dir = std::filesystem::path(__FILE__).parent_path();
    return test_dir / "test_output";
}

void ensure_test_directories() {
    const auto data_dir = get_test_data_dir();
    const auto output_dir = get_test_output_dir();

    std::filesystem::create_directories(data_dir / "videos");
    std::filesystem::create_directories(data_dir / "roi_data");
    std::filesystem::create_directories(data_dir / "configs");
    std::filesystem::create_directories(output_dir / "encoded");
    std::filesystem::create_directories(output_dir / "frames");
    std::filesystem::create_directories(output_dir / "logs");
}

void cleanup_test_output(bool keep_dirs) {
    const auto output_dir = get_test_output_dir();

    if (std::filesystem::exists(output_dir)) {
        for (const auto &entry: std::filesystem::recursive_directory_iterator(output_dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::remove(entry.path());
            }
        }

        if (!keep_dirs) {
            std::filesystem::remove_all(output_dir);
            ensure_test_directories();
        }
    }
}

bool create_static_roi_json(const std::filesystem::path &output_path,
                            const std::vector<std::tuple<int, int, int, int>> &rois) {
    // Ensure output directory exists
    const auto parent_dir = output_path.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        std::filesystem::create_directories(parent_dir);
    }

    std::ofstream json_file(output_path);
    if (!json_file.is_open()) {
        return false;
    }

    // Write JSON structure
    json_file << "{\n";
    json_file << "  \"rois\": [\n";

    for (size_t i = 0; i < rois.size(); ++i) {
        const auto &[x, y, width, height] = rois[i];
        json_file << "    {\n";
        json_file << "      \"x\": " << x << ",\n";
        json_file << "      \"y\": " << y << ",\n";
        json_file << "      \"width\": " << width << ",\n";
        json_file << "      \"height\": " << height << "\n";
        json_file << "    }";

        if (i < rois.size() - 1) {
            json_file << ",";
        }
        json_file << "\n";
    }

    json_file << "  ]\n";
    json_file << "}\n";

    json_file.close();
    return std::filesystem::exists(output_path);
}

bool create_roi_data_file(const std::filesystem::path &output_path, const std::string &sequence_name, int width,
                          int height, int framerate,
                          const std::vector<std::vector<std::tuple<int, int, int, int>>> &frame_rois) {
    (void) sequence_name;
    (void) width;
    (void) height;
    (void) framerate;

    // Ensure output directory exists
    const auto parent_dir = output_path.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        std::filesystem::create_directories(parent_dir);
    }

    std::ofstream roi_file(output_path);
    if (!roi_file.is_open()) {
        return false;
    }

    // Write ROI data in Python list format: [[[x1,y1,x2,y2], ...], ...]
    // Note: The parser expects format [[...]] (double brackets), not [[[...]]] (triple)
    // Outer brackets are for the file format, inner brackets are for frames
    roi_file << "[";

    for (size_t frame_idx = 0; frame_idx < frame_rois.size(); ++frame_idx) {
        const auto &rois = frame_rois[frame_idx];

        roi_file << "[";
        for (size_t roi_idx = 0; roi_idx < rois.size(); ++roi_idx) {
            const auto &[x1, y1, x2, y2] = rois[roi_idx];
            roi_file << "[" << x1 << "," << y1 << "," << x2 << "," << y2 << "]";
            if (roi_idx < rois.size() - 1) {
                roi_file << ",";
            }
        }
        roi_file << "]";

        if (frame_idx < frame_rois.size() - 1) {
            roi_file << ",";
        }
    }

    roi_file << "]";

    roi_file.flush();
    roi_file.close();

    // Verify file was written correctly
    if (!std::filesystem::exists(output_path)) {
        return false;
    }

    // Verify file has content
    const auto file_size = std::filesystem::file_size(output_path);
    return file_size > 0;
}

bool write_text_file(const std::filesystem::path &output_path, const std::string &file_contents) {
    const auto parent_dir = output_path.parent_path();
    if (!parent_dir.empty() && !std::filesystem::exists(parent_dir)) {
        std::filesystem::create_directories(parent_dir);
    }

    std::ofstream output_file(output_path);
    if (!output_file.is_open()) {
        return false;
    }

    output_file << file_contents;
    output_file.flush();
    output_file.close();

    return std::filesystem::exists(output_path) && std::filesystem::file_size(output_path) > 0;
}

bool validate_config_file(const std::filesystem::path &config_path, const std::string &required_token,
                          std::string &error_message) {
    if (!std::filesystem::exists(config_path)) {
        error_message = "Config file does not exist: " + config_path.string();
        return false;
    }

    if (std::filesystem::file_size(config_path) == 0) {
        error_message = "Config file is empty: " + config_path.string();
        return false;
    }

    if (required_token.empty()) {
        return true;
    }

    std::ifstream verify_file(config_path);
    if (!verify_file.is_open()) {
        error_message = "Failed to open config file: " + config_path.string();
        return false;
    }

    std::string line;
    while (std::getline(verify_file, line)) {
        if (line.find(required_token) != std::string::npos) {
            return true;
        }
    }

    error_message = "Config file missing required token '" + required_token + "': " + config_path.string();
    return false;
}

bool parse_test_command_line(const std::filesystem::path &config_path, command_line_parameters &params,
                             std::string &error_message) {
    const std::string config_arg = "--config";
    const std::string config_value = config_path.string();
    const char *argv[] = {"test_encoder", config_arg.c_str(), config_value.c_str(), nullptr};
    const int argc = 3;

    try {
        params = parse_command_line(argc, const_cast<char **>(argv));
    } catch (const std::exception &exception) {
        error_message = std::string("Failed to parse command line: ") + exception.what();
        return false;
    }

    return true;
}

bool load_encoder_config_from_file(const std::filesystem::path &config_path, encoder_config &config,
                                   std::string &error_message) {
    command_line_parameters params;
    if (!parse_test_command_line(config_path, params, error_message)) {
        return false;
    }

    try {
        config = load_and_configure_encoder(params);
    } catch (const std::exception &exception) {
        error_message = std::string("Failed to load configuration: ") + exception.what();
        return false;
    }

    return true;
}

pipeline_setup_result create_full_test_pipelines(global_data &data) {
    pipeline_setup_result result{};

    try {
        if (!create_input_pipeline(data)) {
            result.status = pipeline_setup_status::failure;
            result.error_message = "Input pipeline creation failed";
            return result;
        }
        if (!create_encoding_pipeline(data)) {
            result.status = pipeline_setup_status::encoding_unavailable;
            result.error_message = "Encoding pipeline creation failed";
            return result;
        }
        if (!create_output_pipeline(data)) {
            result.status = pipeline_setup_status::failure;
            result.error_message = "Output pipeline creation failed";
            return result;
        }
    } catch (const std::exception &exception) {
        result.status = pipeline_setup_status::failure;
        result.error_message = std::string("Exception during pipeline creation: ") + exception.what();
        return result;
    }

    result.status = pipeline_setup_status::success;
    return result;
}

void cleanup_test_global_data(std::unique_ptr<global_data> &data, const bool reset_udp_state) {
    if (!data) {
        return;
    }

    data->stop_processing = true;
    data->raw_frame_queue.close();
    data->encoded_frame_queue.close();
    data->packed_regions_queue.close();

    if (reset_udp_state) {
        input_ingress_facade view(*data);
        reset_udp_connection_state(view);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    data.reset();
}

// Initialize encoder_cli_main debug category for tests
void init_encoder_cli_main_debug_category() {
    GST_DEBUG_CATEGORY_INIT(encoder_cli_main, "encoder-cli", 0, "Encoder CLI application");
}
