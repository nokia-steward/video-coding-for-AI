// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "camera_input.h"
#include "../../../libvcai/include/logging.h"
#include "common_input.h"
#include "input_ingress_facade.h"

#include <gst/gst.h>

/**
 * @file camera_input.cpp
 * @brief Implementation of camera input pipeline creation and management.
 */

/// GStreamer debug category for camera input handling.
GST_DEBUG_CATEGORY_STATIC(camera_input);

void init_camera_input_debug_category() {
    GST_DEBUG_CATEGORY_INIT(camera_input, "camera-input", 0, "Camera input pipeline operations");
}

bool create_camera_input_elements(const encoder_config &data, GstElement *&v4l2src, GstElement *&queue,
                                  GstElement *&capsfilter, GstElement *&tee) {
    v4l2src = gst_element_factory_make("v4l2src", "camera-source");
    if (!v4l2src) {
        sf::error(camera_input, "Failed to create v4l2src element - check if V4L2 plugins are available");
        return false;
    }

    queue = gst_element_factory_make("queue", "camera-queue");
    if (!queue) {
        sf::error(camera_input, "Failed to create queue element");
        return false;
    }

    capsfilter = gst_element_factory_make("capsfilter", "camera-caps");
    if (!capsfilter) {
        sf::error(camera_input, "Failed to create capsfilter element");
        return false;
    }

    tee = gst_element_factory_make("tee", "input-tee");
    if (!tee) {
        sf::error(camera_input, "Failed to create tee element");
        return false;
    }

    // Set camera device with optimal io-mode for memory mapping
    // Don't set restrictive caps - let v4l2src negotiate with the camera to find a compatible format
    // The camera will output its native format, and we'll handle conversion downstream
    g_object_set(v4l2src, "device", data.camera_device.c_str(), "io-mode", 2, nullptr);

    if (data.width > 0 && data.height > 0 && data.framerate > 0) {
        sf::info(camera_input,
                 "Camera parameters requested: %dx%d@%dfps (camera will negotiate actual supported format)", data.width,
                 data.height, data.framerate);
    } else {
        sf::info(camera_input, "No specific camera resolution requested, camera will use its default format");
    }

    // Don't set caps on capsfilter - let it pass through whatever v4l2src negotiates with the camera
    // This allows the camera to output what it supports, and we'll convert downstream as needed

    // Configure queue as "shock absorber" with downstream leaky behavior
    // Set to 12 buffers: provides substantial smoothing to reduce jerkiness
    g_object_set(queue, "leaky", 2, "max-size-buffers", 12, nullptr);

    sf::info(camera_input, "Camera input configured: device=%s, resolution=%dx%d, framerate=%d",
             data.camera_device.c_str(), data.width, data.height, data.framerate);

    return true;
}

/**
 * @brief Builds the camera input pipeline and attaches it to global data.
 * @param data Global data structure to populate with elements.
 * @return True if the pipeline was created successfully.
 */
bool create_camera_input_pipeline(global_data &data) {
    input_ingress_facade facade(data);
    sf::info(camera_input, "Starting camera input pipeline creation");
    sf::info(camera_input, "Camera configuration: device=%s, resolution=%dx%d, framerate=%d",
             facade.params.camera_device.c_str(), facade.params.width, facade.params.height, facade.params.framerate);

    GstElement *v4l2src, *queue, *capsfilter, *tee;

    if (!create_camera_input_elements(facade.params, v4l2src, queue, capsfilter, tee)) {
        sf::error(camera_input, "Failed to create camera input elements");
        return false;
    }

    // Create JPEG decoder and video converter for camera input
    // Use software JPEG decoder to avoid nvdec compatibility issues with JPEG format
    // Note: If camera outputs raw video instead of MJPEG, format negotiation will handle it
    GstElement *jpegdec = gst_element_factory_make("jpegdec", "camera-jpeg-decoder");
    if (!jpegdec) {
        sf::error(camera_input, "Failed to create software JPEG decoder element (jpegdec)");
        return false;
    }

    sf::info(camera_input, "Using software JPEG decoder (jpegdec) for camera input");

    GstElement *nvvideoconvert = gst_element_factory_make("nvvideoconvert", "camera-nvvideo-convert");
    if (!nvvideoconvert) {
        sf::error(camera_input, "Failed to create NVIDIA video converter element (nvvideoconvert)");
        return false;
    }

    input_sink_branch_elements sink_elements{};
    if (!create_input_sink_branch_elements(facade, "input-sink", "Camera input pipeline", sink_elements)) {
        sf::error(camera_input, "Failed to initialize camera sink branch elements");
        return false;
    }

    // Add elements to pipeline
    // capsfilter is kept in pipeline but without restrictive caps to allow pass-through
    gst_bin_add_many(GST_BIN(facade.input_pipeline.get()), v4l2src, queue, capsfilter, jpegdec, nvvideoconvert, tee,
                     sink_elements.appsink, nullptr);

    if (facade.params.debug.save_frames) {
        gst_bin_add_many(GST_BIN(facade.input_pipeline.get()), sink_elements.queue_original,
                         sink_elements.converter_original, sink_elements.encoder_original, sink_elements.sink_original,
                         nullptr);
    }

    // Link elements: v4l2src -> queue -> capsfilter -> jpegdec -> nvvideoconvert -> tee -> appsink
    // Note: If camera outputs raw video instead of MJPEG, format negotiation will handle it
    // GStreamer will negotiate the best format between v4l2src and downstream elements
    bool result = gst_element_link_many(v4l2src, queue, capsfilter, jpegdec, nvvideoconvert, tee, sink_elements.appsink,
                                        nullptr);
    if (!result) {
        sf::error(camera_input, "Failed to link camera pipeline elements: v4l2src -> queue -> capsfilter -> jpegdec -> "
                                "nvvideoconvert -> tee -> appsink");
        sf::error(camera_input,
                  "This may indicate the camera outputs a format incompatible with jpegdec (e.g., raw video)");
        return false;
    }

    result = link_original_frame_saving_branch(tee, sink_elements.queue_original, sink_elements.converter_original,
                                               sink_elements.encoder_original, sink_elements.sink_original);
    if (!result) {
        sf::error(camera_input, "Failed to link original frame saving branch");
        return false;
    }

    return result;
}
