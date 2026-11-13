// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "paths.h"
#include "../../../libvcai/include/logging.h"
#include "elements.h"
#include "output_pipeline_debug.h"

#include <filesystem>
#include <gst/gst.h>

/**
 * @file paths.cpp
 * @brief Implementation of output file path configuration.
 */

namespace {
    /**
     * @brief Creates output directory.
     * @param output_dir Directory path.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_output_directory(const std::string &output_dir) {
        try {
            if (std::filesystem::create_directories(output_dir)) {
                sf::info(output_pipeline, "Created %s directory for model-specific output files", output_dir.c_str());
            }
            return true;
        } catch (const std::filesystem::filesystem_error &e) {
            sf::warning(output_pipeline, "Failed to create %s directory: %s", output_dir.c_str(), e.what());
            return false;
        }
    }

    /**
     * @brief Configures H.265 file sink path.
     * @param elements The pipeline elements.
     * @param h265_filename File path.
     */
    void configure_h265_file_sink(const output_pipeline_elements &elements, const std::string &h265_filename) {
        if (elements.sink_file) {
            g_object_set(elements.sink_file, "location", h265_filename.c_str(), nullptr);
        }
    }

    /**
     * @brief Configures MP4 file sink path.
     * @param elements The pipeline elements.
     * @param mp4_filename File path.
     */
    void configure_mp4_file_sink(const output_pipeline_elements &elements, const std::string &mp4_filename) {
        if (elements.sink_mp4) {
            g_object_set(elements.sink_mp4, "location", mp4_filename.c_str(), nullptr);
        }
    }

    /**
     * @brief Clears existing debug frames directory.
     * @param debug_dir Directory path.
     */
    void clear_debug_frames_directory(const std::string &debug_dir) {
        if (std::filesystem::exists(debug_dir)) {
            sf::info(output_pipeline, "Clearing existing debug frames from %s", debug_dir.c_str());
            for (const auto &entry: std::filesystem::directory_iterator(debug_dir)) {
                if (std::filesystem::is_regular_file(entry.path())) {
                    std::filesystem::remove(entry.path());
                }
            }
        }
    }

    /**
     * @brief Creates debug frames directory.
     * @param debug_dir Directory path.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_debug_frames_directory(const std::string &debug_dir) {
        try {
            clear_debug_frames_directory(debug_dir);
            if (std::filesystem::create_directories(debug_dir)) {
                sf::info(output_pipeline, "Created %s directory for processed frame saving", debug_dir.c_str());
            }
            return true;
        } catch (const std::filesystem::filesystem_error &e) {
            sf::warning(output_pipeline, "Failed to create %s directory: %s", debug_dir.c_str(), e.what());
            return false;
        }
    }

    /**
     * @brief Configures debug frame sink path.
     * @param elements The pipeline elements.
     * @param debug_location File path pattern.
     */
    void configure_debug_frame_sink(const output_pipeline_elements &elements, const std::string &debug_location) {
        g_object_set(elements.sink_debug, "location", debug_location.c_str(), nullptr);
        g_object_set(elements.sink_debug, "max-files", 0, nullptr);
    }
} // namespace

std::string determine_model_name() { return "yolo"; }

bool configure_output_paths(const encoder_config &config, const output_pipeline_elements &elements) {
    const std::string model_name = determine_model_name();
    const std::string output_dir = "../" + model_name;
    std::string base_filename = output_dir + "/" + model_name + "_output_with_sei";
    if (config.pack_rois) {
        base_filename += "_packed";
    }
    const std::string h265_filename = base_filename + ".h265";
    const std::string mp4_filename = base_filename + ".mp4";

    if (!create_output_directory(output_dir)) {
        return false;
    }

    configure_h265_file_sink(elements, h265_filename);
    configure_mp4_file_sink(elements, mp4_filename);

    if (config.debug.save_frames) {
        const std::filesystem::path debug_dir_path = resolve_frame_output_root_directory(config) / "debug_frames";
        const std::string debug_dir = debug_dir_path.string();
        if (!create_debug_frames_directory(debug_dir)) {
            return false;
        }
        const std::string debug_location = debug_dir + "/frame_%06d.png";
        configure_debug_frame_sink(elements, debug_location);
    }

    return true;
}
