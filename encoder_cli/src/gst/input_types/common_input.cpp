// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "common_input.h"
#include "../../../libvcai/include/area_copy.h"
#include "../../../libvcai/include/logging.h"
#include "../../encoder_enums.h"
#include "../gst_callbacks.h"
#include "../output_pipeline/paths.h"
#include "../pipeline_utils.h"

#include <filesystem>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string>

/**
 * @file common_input.cpp
 * @brief Implementation of common utilities and shared elements for input pipelines.
 */

/// GStreamer debug category for common input utilities.
GST_DEBUG_CATEGORY_STATIC(common_input);

void init_common_input_debug_category() {
    GST_DEBUG_CATEGORY_INIT(common_input, "common-input", 0, "Common input pipeline utilities");
}

/**
 * @brief Creates common input elements shared by file/camera/ROI pipelines.
 * @param data The input/ingress runtime slice.
 * @param filesrc Output pointer for filesrc.
 * @param parser Output pointer for rawvideoparse.
 * @param tee Output pointer for the input tee.
 * @return True if all elements were created successfully.
 */
bool create_common_input_elements(input_ingress_facade &data, GstElement *&filesrc, GstElement *&parser,
                                  GstElement *&tee) {
    sf::debug(common_input, "Creating common input elements: filesrc, rawvideoparse, tee");
    data.expected_file_input_frames.reset();
    data.file_input_forced_raw_eos = false;

    const auto log_file_input_details = [&data]() {
        if (data.params.input_type_value != input_type::file) {
            return;
        }

        const char *file_name = data.params.file_name.get();
        if (!file_name || strlen(file_name) == 0) {
            sf::error(common_input, "File input selected but file_name is empty");
            return;
        }

        const std::filesystem::path file_path(file_name);
        const std::filesystem::path absolute_path = std::filesystem::absolute(file_path);
        sf::info(common_input, "File input path: '%s' (absolute: '%s')", file_name, absolute_path.string().c_str());

        if (!std::filesystem::exists(file_path)) {
            sf::error(common_input, "File input path does not exist: %s", absolute_path.string().c_str());
            return;
        }

        std::error_code error_code;
        const auto file_size = std::filesystem::file_size(file_path, error_code);
        if (error_code) {
            sf::warning(common_input, "Failed to get file size for %s: %s", absolute_path.string().c_str(),
                        error_code.message().c_str());
            return;
        }

        sf::info(common_input, "File input size: %zu bytes", static_cast<size_t>(file_size));

        if (data.params.width <= 0 || data.params.height <= 0) {
            sf::warning(common_input, "File input dimensions not set (width=%d, height=%d)", data.params.width,
                        data.params.height);
            return;
        }

        if (!data.params.format.has_value()) {
            sf::warning(common_input, "File input format not set; cannot validate frame size");
            return;
        }

        const auto format_value = *data.params.format;
        if (format_value != sf::video_format::i420 && format_value != sf::video_format::nv12 &&
            format_value != sf::video_format::yuv420) {
            sf::warning(common_input, "File input format '%s' is not validated for frame sizing",
                        sf::video_format_to_string(format_value));
            return;
        }

        const size_t frame_size =
                static_cast<size_t>(data.params.width) * static_cast<size_t>(data.params.height) * 3U / 2U;
        if (frame_size == 0U) {
            sf::warning(common_input, "Computed frame size is zero; skipping file size validation");
            return;
        }

        if (file_size % frame_size != 0U) {
            sf::warning(common_input, "File size (%zu) is not a multiple of frame size (%zu) for %dx%d %s input",
                        static_cast<size_t>(file_size), frame_size, data.params.width, data.params.height,
                        sf::video_format_to_string(format_value));
            return;
        }

        const size_t frame_count = static_cast<size_t>(file_size) / frame_size;
        sf::info(common_input, "File input frame count: %zu (frame size: %zu bytes)", frame_count, frame_size);
        if (frame_count > 0U) {
            data.expected_file_input_frames = static_cast<guint64>(frame_count);
        }
    };

    log_file_input_details();

    filesrc = gst_element_factory_make("filesrc", "file-source");
    if (!filesrc) {
        sf::error(common_input, "Failed to create filesrc element. gstreamer-core plugin may not be available.");
        return false;
    }

    parser = gst_element_factory_make("rawvideoparse", "raw-video-parser");
    if (!parser) {
        sf::error(common_input,
                  "Failed to create rawvideoparse element. gstreamer-plugins-base plugin may not be available.");
        return false;
    }
    data.raw_video_parser = parser;

    tee = gst_element_factory_make("tee", "input-tee");
    if (!tee) {
        sf::error(common_input, "Failed to create tee element. gstreamer-core plugin may not be available.");
        return false;
    }

    sf::debug(common_input, "Common input elements created successfully");

    g_object_set(filesrc, "location", data.params.file_name.get(), nullptr);
    g_object_set(parser, "width", data.params.width, "height", data.params.height, "framerate", data.params.framerate,
                 1, nullptr);
    if (data.params.format.has_value()) {
        const char *format_str = sf::video_format_to_string(*data.params.format);
        const GstVideoFormat gst_format = gst_video_format_from_string(format_str);
        if (gst_format == GST_VIDEO_FORMAT_UNKNOWN) {
            sf::warning(common_input, "Unknown raw video format '%s'; rawvideoparse format not set", format_str);
        } else {
            g_object_set(parser, "format", gst_format, nullptr);
            sf::info(common_input, "Raw video parser format set to %s", format_str);
        }
    } else {
        sf::warning(common_input, "Raw video format not specified; rawvideoparse will use its default");
    }

    return true;
}

bool create_original_frame_saving_elements(const encoder_config &data, const std::string & /* model_name */,
                                           GstElement *&queue_original, GstElement *&converter_original,
                                           GstElement *&encoder_original, GstElement *&sink_original) {
    if (!data.debug.save_frames) {
        return true; // Not enabled, return success
    }

    queue_original = gst_element_factory_make("queue", "q-orig");
    if (!queue_original) {
        sf::error(common_input, "Failed to create queue element for original frame saving");
        return false;
    }

    // Use software videoconvert instead of nvvideoconvert.  The input pipeline
    // provides raw video in system memory (from rawvideoparse), so there is no
    // need for GPU conversion.  Using nvvideoconvert here would output NVMM
    // memory that pngenc cannot handle, causing a caps negotiation failure.
    converter_original = gst_element_factory_make("videoconvert", "c-orig");
    if (!converter_original) {
        sf::error(common_input, "Failed to create videoconvert element for original frame saving");
        return false;
    }

    encoder_original = gst_element_factory_make("pngenc", "e-orig");
    if (!encoder_original) {
        sf::error(common_input, "Failed to create pngenc element for original frame saving");
        return false;
    }

    sink_original = gst_element_factory_make("multifilesink", "s-orig");
    if (!sink_original) {
        sf::error(common_input, "Failed to create multifilesink element for original frame saving");
        return false;
    }

    // async=false prevents the multifilesink from blocking the input pipeline's
    // PAUSED→PLAYING state transition while waiting for preroll data.  Without
    // this, the input pipeline stays at READY/PAUSED and never produces frames.
    g_object_set(sink_original, "async", FALSE, nullptr);

    // Create original_frames under the configured frame output root.
    const std::filesystem::path original_dir_path = resolve_frame_output_root_directory(data) / "original_frames";
    const std::string original_dir = original_dir_path.string();
    try {
        // Clear existing files if directory exists
        if (std::filesystem::exists(original_dir)) {
            sf::info(common_input, "Clearing existing original frames from %s", original_dir.c_str());
            for (const auto &entry: std::filesystem::directory_iterator(original_dir)) {
                if (std::filesystem::is_regular_file(entry.path())) {
                    std::filesystem::remove(entry.path());
                }
            }
        }

        if (std::filesystem::create_directories(original_dir)) {
            sf::info(common_input, "Created %s directory for original frame saving", original_dir.c_str());
        }
    } catch (const std::filesystem::filesystem_error &e) {
        sf::warning(common_input, "Failed to create %s directory: %s", original_dir.c_str(), e.what());
    }

    const std::string original_location = original_dir + "/original_frame_%06d.png";
    g_object_set(sink_original, "location", original_location.c_str(), nullptr);
    g_object_set(sink_original, "max-files", 0, nullptr); // No limit on number of files

    return true;
}

bool create_input_sink_branch_elements(input_ingress_facade &data, const char *appsink_name, const char *pipeline_label,
                                       input_sink_branch_elements &sink_elements) {
    const char *resolved_pipeline_label = pipeline_label ? pipeline_label : "input pipeline";

    sink_elements.appsink = gst_element_factory_make("appsink", appsink_name);
    if (!sink_elements.appsink) {
        sf::error(common_input, "%s failed - could not create appsink element", resolved_pipeline_label);
        return false;
    }

    if (!configure_appsink(sink_elements.appsink, data)) {
        sf::error(common_input, "%s failed - could not configure appsink element", resolved_pipeline_label);
        return false;
    }

    const std::string model_name = determine_model_name();
    if (!create_original_frame_saving_elements(data.params, model_name, sink_elements.queue_original,
                                               sink_elements.converter_original, sink_elements.encoder_original,
                                               sink_elements.sink_original)) {
        sf::error(common_input, "%s failed - could not create original frame saving elements", resolved_pipeline_label);
        return false;
    }

    return true;
}

/**
 * @brief Configures appsink properties for input pipelines.
 * @param appsink Appsink element to configure.
 * @param data The input/ingress runtime slice.
 * @return True if configuration succeeded.
 */
bool configure_appsink(GstElement *appsink, input_ingress_facade &data) {
    if (!appsink) {
        sf::error(common_input, "Appsink element is null");
        return false;
    }

    // Set to 8 buffers: provides more smoothing to reduce jerkiness
    // sync=false allows pipeline to transition to PLAYING without waiting for downstream
    // Since we're pushing to a queue (not a real downstream element), sync=false is appropriate
    g_object_set(appsink, "emit-signals", TRUE, "max-buffers", 8, "sync", false, nullptr);
    sf::debug(common_input, "Configured appsink: max-buffers=8, sync=false (queue-based, enhanced smoothing)");

    // Callback entry points stay in gst_callbacks.cpp, but ingress queue policy is now delegated
    // to ingress bridge orchestrator functions.
    g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_raw_sample), &data.instance());
    g_signal_connect(appsink, "eos", G_CALLBACK(on_raw_eos), &data.instance());

    // For UDP input, use more flexible caps to allow proper negotiation from decoded stream
    // For file/camera input, use specific format from config
    sf::unique_gst_caps_ptr caps;
    if (data.params.input_type_value == input_type::udp) {
        // UDP: Allow format negotiation, specify expected resolution if available
        if (data.params.width > 0 && data.params.height > 0) {
            const char *format_str =
                    data.params.format.has_value() ? sf::video_format_to_string(*data.params.format) : "NV12";
            caps = sf::unique_gst_caps_ptr(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str,
                                                               "width", G_TYPE_INT, data.params.width, "height",
                                                               G_TYPE_INT, data.params.height, nullptr));
        } else {
            // If resolution not specified, just use format (let negotiation handle the rest)
            const char *format_str =
                    data.params.format.has_value() ? sf::video_format_to_string(*data.params.format) : "NV12";
            caps = sf::unique_gst_caps_ptr(
                    gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str, nullptr));
        }
    } else {
        // File/Camera: Use format from config
        if (!data.params.format.has_value()) {
            sf::error(common_input, "Format not set, cannot create caps");
            return false;
        }
        const char *format_str = sf::video_format_to_string(*data.params.format);
        caps = sf::unique_gst_caps_ptr(
                gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str, nullptr));
    }

    if (!caps) {
        sf::error(common_input, "Failed to create caps for appsink");
        return false;
    }

    // Verify caps is valid before setting
    if (!GST_IS_CAPS(caps.get())) {
        sf::error(common_input, "Created caps object is not a valid GstCaps");
        return false;
    }

    g_object_set(appsink, "caps", caps.get(), nullptr);
    sf::debug(common_input, "Configured appsink caps for input type: %s",
              input_type_to_string(data.params.input_type_value));

    return true;
}

bool link_original_frame_saving_branch(GstElement *tee, GstElement *queue_original, GstElement *converter_original,
                                       GstElement *encoder_original, GstElement *sink_original) {
    if (!queue_original || !converter_original || !encoder_original || !sink_original) {
        return true; // Not enabled, return success
    }

    if (!gst_element_link_many(queue_original, converter_original, encoder_original, sink_original, nullptr)) {
        sf::error(common_input, "Failed to link original frame saving elements: queue -> converter -> encoder -> sink");
        return false;
    }

    if (!link_tee_to_branch(tee, queue_original)) {
        sf::error(common_input, "Failed to link tee to original frame saving branch");
        return false;
    }

    return true;
}
