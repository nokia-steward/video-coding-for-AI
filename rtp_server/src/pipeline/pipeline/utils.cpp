// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "utils.h"
#include "../pipeline.h"
#include "logging.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <gst/gst.h>
#include <iomanip>
#include <sstream>

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

std::string generate_filename(const rtp_pipeline &pipeline) {
    const auto now = std::chrono::system_clock::now();
    const auto time_value = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    // Generate the filename
    std::stringstream file_name;
    file_name << "rtp_stream_port" << pipeline.port << "_"
              << std::put_time(std::localtime(&time_value), "%Y%m%d_%H%M%S") << "_" << std::setfill('0') << std::setw(3)
              << ms.count() << ".h265";

    std::string generated_filename = file_name.str();

    // Use output_directory if specified
    if (pipeline.output_directory.has_value() && !pipeline.output_directory->empty()) {
        std::filesystem::path full_path(*pipeline.output_directory);
        full_path /= generated_filename;
        return full_path.string();
    }

    // Fallback: If VCAI_DATA_DIR is set (e.g., in Docker), prepend the directory path
    if (const char *data_dir = getenv("VCAI_DATA_DIR"); data_dir && strlen(data_dir) > 0) {
        const std::string data_dir_str(data_dir);
        std::filesystem::path full_path(data_dir_str);
        full_path /= "rtp_server";
        full_path /= generated_filename;
        return full_path.string();
    }

    // When not in Docker and no output directory specified, return just the filename
    // (This should not happen if output_directory validation is correct, but kept for backward compatibility)
    return generated_filename;
}

std::filesystem::path get_debug_frames_root_directory(const rtp_pipeline &pipeline) {
    if (pipeline.output_directory.has_value() && !pipeline.output_directory->empty()) {
        return std::filesystem::path(*pipeline.output_directory);
    }

    if (pipeline.server_config && !pipeline.server_config->config_directory.empty()) {
        return pipeline.server_config->config_directory;
    }

    return std::filesystem::current_path();
}

std::filesystem::path get_debug_frames_port_directory(const rtp_pipeline &pipeline) {
    std::filesystem::path debug_frames_dir = get_debug_frames_root_directory(pipeline);
    debug_frames_dir /= "debug_frames";
    debug_frames_dir /= "port_" + std::to_string(static_cast<unsigned int>(pipeline.port));
    return debug_frames_dir;
}

void cleanup_elements(rtp_pipeline &pipeline) {
    sf::info(pipeline_category, "Cleaning up pipeline elements for port %u...", pipeline.port);

    constexpr GstClockTime state_timeout = 2 * GST_SECOND;

    if (pipeline.main_pipeline) {
        gst_element_set_state(pipeline.main_pipeline.get(), GST_STATE_NULL);
        gst_element_get_state(pipeline.main_pipeline.get(), nullptr, nullptr, state_timeout);
    }

    pipeline.output_controller.cleanup(pipeline);

    pipeline.rtp_input_source.reset();
    pipeline.webrtcbin.reset();
    pipeline.rtp_input_capsfilter.reset();
    pipeline.webrtc_rtp_capsfilter.reset();
    pipeline.rtph265depay.reset();
    pipeline.h265parse.reset();
    pipeline.h265_capsfilter.reset();
    pipeline.queue_encoded.reset();
    pipeline.sf_h265_sei_extract.reset();
    pipeline.h265_decoder.reset();
    pipeline.decoded_converter.reset();
    pipeline.decoded_capsfilter.reset();
    pipeline.sf_pri_reconstruct.reset();
    pipeline.sf_eoi_luma_backscale.reset();
    pipeline.sf_tet_apply.reset();
    pipeline.queue_decoded.reset();
    pipeline.gpu_video_convert.reset();
    pipeline.tee.reset();
    pipeline.queue_display.reset();
    pipeline.display_convert.reset();
    pipeline.capsfilter_display.reset();
    pipeline.nvosd_display.reset();
    pipeline.output_sink.reset();
    pipeline.queue_file.reset();
    pipeline.h265_encoder.reset();
    pipeline.filesink.reset();
    pipeline.queue_rtp_output.reset();
    pipeline.h265_encoder_rtp_output.reset();
    pipeline.rtph265pay_rtp_output.reset();
    pipeline.rtp_output_sink.reset();
    pipeline.queue_debug_frames.reset();
    pipeline.debug_frames_download.reset();
    pipeline.debug_frames_convert.reset();
    pipeline.debug_frames_png_encoder.reset();
    pipeline.debug_frames_sink.reset();
    pipeline.nvstreammux.reset();
    pipeline.nvinfer.reset();
    pipeline.nvtracker.reset();
    pipeline.decoded_output_capsfilter.reset();

    pipeline.main_pipeline.reset();

    sf::info(pipeline_category, "Pipeline cleanup completed for port %u", pipeline.port);
}
