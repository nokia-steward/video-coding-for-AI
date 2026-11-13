// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "frame_saver.h"

#include <filesystem>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <sys/stat.h>
#include <utility>

#include "../../libvcai/include/gst_raii.h"
#include "../../libvcai/include/logging.h"

/**
 * @file frame_saver.cpp
 * @brief Implementation of frame saving operations for debugging.
 */

/// GStreamer debug category for frame saving.
GST_DEBUG_CATEGORY_STATIC(frame_saver_category);

void init_frame_saver_debug_category() {
    GST_DEBUG_CATEGORY_INIT(frame_saver_category, "frame-saver", 0, "Frame saving operations for debugging");
}


frame_saver::frame_saver(std::string output_dir_param) : output_dir(std::move(output_dir_param)), enabled(true) {
    create_output_dir();
}

frame_saver::~frame_saver() = default;

void frame_saver::create_output_dir() const {
    // Clear existing files if directory exists
    if (std::filesystem::exists(output_dir)) {
        sf::info(frame_saver_category, "Clearing existing debug frames from %s", output_dir.c_str());
        try {
            for (const auto &entry: std::filesystem::directory_iterator(output_dir)) {
                if (std::filesystem::is_regular_file(entry.path())) {
                    std::filesystem::remove(entry.path());
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            sf::warning(frame_saver_category, "Failed to clear directory %s: %s", output_dir.c_str(), e.what());
        }
    }

    // Create directory if it doesn't exist
    struct stat st {};
    if (stat(output_dir.c_str(), &st) == -1) {
        if (mkdir(output_dir.c_str(), 0755) == 0) {
            sf::info(frame_saver_category, "Created debug frames directory: %s", output_dir.c_str());
        } else {
            sf::error(frame_saver_category, "Failed to create debug frames directory: %s", output_dir.c_str());
        }
    }
}

/**
 * @brief Builds a PNG filename for a frame number.
 * @param frame_number Frame index for the filename.
 * @return Full output path for the PNG.
 */
std::string frame_saver::generate_filename(const guint frame_number) const {
    std::ostringstream oss;
    oss << output_dir << "/frame_" << std::setfill('0') << std::setw(6) << frame_number << ".png";
    return oss.str();
}

/**
 * @brief Converts a YUV420 buffer to PNG and writes it to disk.
 * @param buffer Source buffer containing YUV420 data.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param frame_number Frame index for naming.
 * @return True if the frame was saved successfully.
 */
bool frame_saver::save_frame(GstBuffer *buffer, const guint width, const guint height, const guint frame_number) const {
    if (!enabled || !buffer) {
        return false;
    }

    try {
        // Create a GStreamer pipeline for YUV420 to PNG conversion
        const std::string pipeline_str = "appsrc name=src ! "
                                         "video/x-raw,format=I420,width=" +
                                         std::to_string(width) + ",height=" + std::to_string(height) +
                                         " ! "
                                         "nvvideoconvert ! "
                                         "pngenc ! "
                                         "filesink name=sink";

        GError *error = nullptr;
        const sf::unique_gst_element_ptr pipeline(gst_parse_launch(pipeline_str.c_str(), &error));

        if (!pipeline) {
            if (error) {
                sf::error(frame_saver_category, "Failed to create pipeline: %s", error->message);
                g_error_free(error);
            }
            return false;
        }

        // Get elements
        const sf::unique_gst_element_ptr src(gst_bin_get_by_name(GST_BIN(pipeline.get()), "src"));
        const sf::unique_gst_element_ptr sink(gst_bin_get_by_name(GST_BIN(pipeline.get()), "sink"));

        if (!src || !sink) {
            sf::error(frame_saver_category, "Failed to get pipeline elements");
            return false;
        }

        // Set output filename
        const std::string filename = generate_filename(frame_number);
        g_object_set(G_OBJECT(sink.get()), "location", filename.c_str(), nullptr);

        // Set pipeline state to playing
        GstStateChangeReturn ret = gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            sf::error(frame_saver_category, "Failed to set pipeline to playing state");
            return false;
        }

        // Wait for pipeline to be ready
        GstState state;
        ret = gst_element_get_state(pipeline.get(), &state, nullptr, GST_CLOCK_TIME_NONE);
        if (ret != GST_STATE_CHANGE_SUCCESS) {
            sf::error(frame_saver_category, "Failed to get pipeline to playing state");
            return false;
        }

        // Push buffer to pipeline
        if (const GstFlowReturn flow_ret = gst_app_src_push_buffer(GST_APP_SRC(src.get()), gst_buffer_ref(buffer));
            flow_ret != GST_FLOW_OK) {
            sf::error(frame_saver_category, "Failed to push buffer to pipeline");
            return false;
        }

        // Send EOS to finish processing
        gst_app_src_end_of_stream(GST_APP_SRC(src.get()));

        // Wait for EOS to propagate
        const sf::unique_gst_bus_ptr bus(gst_element_get_bus(pipeline.get()));
        const sf::unique_gst_message_ptr msg(gst_bus_timed_pop_filtered(
                bus.get(), GST_CLOCK_TIME_NONE, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)));

        if (msg) {
            if (GST_MESSAGE_TYPE(msg.get()) == GST_MESSAGE_ERROR) {
                GError *err;
                gchar *debug_info;
                gst_message_parse_error(msg.get(), &err, &debug_info);
                const auto err_raii = sf::unique_g_error_ptr(err);
                const auto debug_info_raii = sf::unique_g_char_ptr(debug_info);
                sf::error(frame_saver_category, "Pipeline error: %s", err_raii->message);
                if (debug_info_raii) {
                    sf::error(frame_saver_category, "Debug info: %s", debug_info_raii.get());
                }
                return false;
            }
        }
        // Cleanup - all RAII objects automatically cleaned up
        gst_element_set_state(pipeline.get(), GST_STATE_NULL);

        sf::info(frame_saver_category, "Saved frame %u to %s", frame_number, filename.c_str());
        return true;

    } catch (const std::exception &e) {
        sf::error(frame_saver_category, "Exception in save_frame: %s", e.what());
        return false;
    }
}
