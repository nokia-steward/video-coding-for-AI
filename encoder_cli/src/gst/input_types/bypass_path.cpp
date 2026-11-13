// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "bypass_path.h"

#include "../../../../libvcai/include/logging.h"
#include "../../encoder_enums.h"
#include "../gpu_elements.h"
#include "common_input.h"

#include <gst/gst.h>

/**
 * @file bypass_path.cpp
 * @brief Implementation of bypass path element creation and management.
 */

/// GStreamer debug category for bypass path handling.
GST_DEBUG_CATEGORY_STATIC(bypass_path);

void init_bypass_path_debug_category() {
    GST_DEBUG_CATEGORY_INIT(bypass_path, "bypass-path", 0, "Bypass path elements for dual appsink");
}

/**
 * @brief Creates GStreamer elements for the bypass path.
 * @param elements Output struct to populate.
 * @return True if all elements were created.
 */
bool create_bypass_path_elements(bypass_path_elements &elements) {
    init_bypass_path_debug_category();

    elements.valve_bypass = gst_element_factory_make("valve", "bypass-valve");
    elements.queue_bypass = gst_element_factory_make("queue", "bypass-queue");
    elements.nvvidconv_bypass = gst_element_factory_make("nvvideoconvert", "bypass-converter");
    elements.capsfilter_bypass = gst_element_factory_make("capsfilter", "bypass-capsfilter");
    // RAII_EXEMPT: bypass_path_elements stores non-owning raw handles used by gst_bin_add paths.
    elements.videoconvert_bypass = create_gpu_video_convert("bypass-video-converter").release();
    elements.appsink_bypass = gst_element_factory_make("appsink", "bypass-sink");

    if (!elements.valve_bypass) {
        sf::error(bypass_path, "Failed to create bypass valve element");
        return false;
    }
    if (!elements.queue_bypass) {
        sf::error(bypass_path, "Failed to create bypass queue element");
        return false;
    }
    if (!elements.nvvidconv_bypass) {
        sf::error(bypass_path, "Failed to create bypass nvvideoconvert element");
        return false;
    }
    if (!elements.capsfilter_bypass) {
        sf::error(bypass_path, "Failed to create bypass capsfilter element");
        return false;
    }
    if (!elements.videoconvert_bypass) {
        sf::error(bypass_path, "Failed to create bypass nvvideoconvert element");
        return false;
    }
    if (!elements.appsink_bypass) {
        sf::error(bypass_path, "Failed to create bypass appsink element");
        return false;
    }

    sf::debug(bypass_path,
              "Created bypass path elements: valve, queue, nvvideoconvert, capsfilter, nvvideoconvert, appsink");
    return true;
}

/**
 * @brief Configures properties for bypass path elements.
 * @param elements Elements to configure.
 * @param data The encoder config (input type, format, dimensions).
 * @return True if configuration succeeded.
 */
bool configure_bypass_path_elements(bypass_path_elements &elements, const encoder_config &data) {
    // Configure valve to drop frames until bypass routing is enabled.
    g_object_set(elements.valve_bypass, "drop", TRUE, nullptr);
    sf::debug(bypass_path, "Configured bypass valve: drop=TRUE to block frames by default");

    // Configure queue: small buffer, non-blocking
    g_object_set(elements.queue_bypass, "max-size-buffers", 5, "max-size-time", 0, "leaky",
                 0, // Don't leak - we need all frames
                 nullptr);
    sf::debug(bypass_path, "Configured bypass queue: max-size-buffers=5, leaky=0");

    // Configure nvvidconv to use GPU compute
    g_object_set(elements.nvvidconv_bypass, "compute-hw", 1, nullptr);
    sf::debug(bypass_path, "Configured bypass nvvideoconvert: compute-hw=1");

    // Configure capsfilter_bypass to force system memory format
    // This ensures NVMM buffers are converted to system memory before appsink
    if (elements.capsfilter_bypass) {
        // Set caps to force system memory format (I420 is a common system memory format)
        sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", nullptr));
        if (caps) {
            g_object_set(elements.capsfilter_bypass, "caps", caps.get(), nullptr);
            sf::debug(bypass_path, "Configured bypass capsfilter to force system memory (I420)");
        }
    }

    // Configure appsink: disable signals (we use polling), no sync
    g_object_set(elements.appsink_bypass, "emit-signals", FALSE, // Disable signals - we use polling
                 "sync", FALSE, "max-buffers", 10, "drop", FALSE, "wait-on-eos", TRUE, // Block pull on EOS
                 nullptr);
    sf::debug(bypass_path, "Configured bypass appsink: emit-signals=FALSE, max-buffers=10, sync=FALSE");

    // Set appsink caps to match inference path output format
    sf::unique_gst_caps_ptr caps;
    if (data.input_type_value == input_type::udp) {
        // UDP: Allow format negotiation, specify expected resolution if available
        if (data.width > 0 && data.height > 0) {
            const char *format_str = data.format.has_value() ? sf::video_format_to_string(*data.format) : "NV12";
            caps = sf::unique_gst_caps_ptr(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str,
                                                               "width", G_TYPE_INT, data.width, "height", G_TYPE_INT,
                                                               data.height, nullptr));
        } else {
            // If resolution not specified, just use format (let negotiation handle the rest)
            const char *format_str = data.format.has_value() ? sf::video_format_to_string(*data.format) : "NV12";
            caps = sf::unique_gst_caps_ptr(
                    gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str, nullptr));
        }
    } else {
        // File/Camera: Use format from config
        if (!data.format.has_value()) {
            sf::error(bypass_path, "Format not set, cannot create caps for bypass appsink");
            return false;
        }
        const char *format_str = sf::video_format_to_string(*data.format);
        caps = sf::unique_gst_caps_ptr(
                gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str, nullptr));
    }

    if (!caps) {
        sf::error(bypass_path, "Failed to create caps for bypass appsink");
        return false;
    }

    if (!GST_IS_CAPS(caps.get())) {
        sf::error(bypass_path, "Created caps object is not a valid GstCaps");
        return false;
    }

    g_object_set(elements.appsink_bypass, "caps", caps.get(), nullptr);
    sf::debug(bypass_path, "Configured bypass appsink caps");

    return true;
}

/**
 * @brief Adds bypass path elements to a pipeline bin.
 * @param pipeline Pipeline bin to add to.
 * @param elements Elements to add.
 * @return True if elements were added.
 */
bool add_bypass_path_to_pipeline(GstElement *pipeline, bypass_path_elements &elements) {
    if (!pipeline) {
        sf::error(bypass_path, "Pipeline is null");
        return false;
    }

    gst_bin_add_many(GST_BIN(pipeline), elements.valve_bypass, elements.queue_bypass, elements.nvvidconv_bypass,
                     elements.capsfilter_bypass, elements.videoconvert_bypass, elements.appsink_bypass, nullptr);

    sf::debug(bypass_path, "Added bypass path elements to pipeline");
    return true;
}

/**
 * @brief Links bypass path elements in order.
 * @param elements Elements to link.
 * @return True if linking succeeded.
 */
bool link_bypass_path_elements(bypass_path_elements &elements) {
    // Link: valve_bypass -> queue_bypass -> nvvidconv_bypass -> capsfilter_bypass -> nvvideoconvert -> appsink_bypass
    // capsfilter forces system memory format, nvvideoconvert converts NVMM to system memory
    if (!gst_element_link_many(elements.valve_bypass, elements.queue_bypass, elements.nvvidconv_bypass,
                               elements.capsfilter_bypass, elements.videoconvert_bypass, elements.appsink_bypass,
                               nullptr)) {
        sf::error(bypass_path, "Failed to link bypass path elements");
        return false;
    }

    sf::debug(
            bypass_path,
            "Linked bypass path elements: valve -> queue -> nvvideoconvert -> capsfilter -> nvvideoconvert -> appsink");
    return true;
}

/**
 * @brief Links the tee to the bypass valve.
 * @param tee Tee element to request a pad from.
 * @param elements Bypass path elements.
 * @return True if linking succeeded.
 */
bool link_tee_to_bypass_path(GstElement *tee, bypass_path_elements &elements) {
    if (!tee) {
        sf::error(bypass_path, "Tee element is null");
        return false;
    }

    // Request a new src pad from the tee
    GstPad *tee_src_pad = gst_element_request_pad_simple(tee, "src_%u");
    if (!tee_src_pad) {
        sf::error(bypass_path, "Failed to request src pad from tee");
        return false;
    }

    // Get static sink pad from valve_bypass
    GstPad *valve_sink_pad = gst_element_get_static_pad(elements.valve_bypass, "sink");
    if (!valve_sink_pad) {
        sf::error(bypass_path, "Failed to get sink pad from bypass valve");
        gst_object_unref(tee_src_pad);
        return false;
    }

    // Link pads
    const GstPadLinkReturn link_result = gst_pad_link(tee_src_pad, valve_sink_pad);
    if (link_result != GST_PAD_LINK_OK) {
        sf::error(bypass_path, "Failed to link tee to bypass valve: %s", gst_pad_link_get_name(link_result));
        gst_object_unref(tee_src_pad);
        gst_object_unref(valve_sink_pad);
        return false;
    }

    // Unref pads after linking
    gst_object_unref(tee_src_pad);
    gst_object_unref(valve_sink_pad);

    sf::debug(bypass_path, "Linked tee to bypass path");
    return true;
}
