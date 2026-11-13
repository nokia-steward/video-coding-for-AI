// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../encoder_config.h"

/**
 * @file bypass_path.height
 * @brief Declares functions and structures for creating and managing the DeepStream bypass path.
 */

/**
 * @brief Initializes the bypass path debug category.
 */
void init_bypass_path_debug_category();

/**
 * @brief GStreamer elements for the DeepStream bypass path.
 *
 * The bypass path provides a fast route for frames that don't need
 * inference processing. It contains minimal elements: a valve for
 * routing control, a queue for buffering, and a video converter
 * to match the output format.
 */
struct bypass_path_elements {
    GstElement *valve_bypass = nullptr; ///< Valve for routing control
    GstElement *queue_bypass = nullptr; ///< Buffer queue for bypass path
    GstElement *nvvidconv_bypass = nullptr; ///< GPU video format converter
    GstElement *capsfilter_bypass = nullptr; ///< Force system memory format for bypass path
    GstElement *videoconvert_bypass = nullptr; ///< Convert NVMM to system memory for bypass appsink
    GstElement *appsink_bypass = nullptr; ///< Output sink for bypass frames
};

/**
 * @brief Creates the GStreamer elements for the bypass path.
 *
 * Creates valve, queue, nvvideoconvert, and appsink elements. Does not add
 * them to a pipeline or link them.
 *
 * @param elements Output struct to populate with created elements.
 * @return True if all elements created successfully, false otherwise.
 */
[[nodiscard]] bool create_bypass_path_elements(bypass_path_elements &elements);

/**
 * @brief Configures the bypass path elements.
 *
 * Sets properties on each element to match the bypass path requirements:
 * - valve: drop=TRUE to block frames until bypass routing is enabled
 * - queue: buffer sizes, leaky behavior
 * - nvvidconv: GPU compute mode
 * - appsink: emit-signals=FALSE, max-buffers, sync
 *
 * @param elements The elements to configure.
 * @param data The encoder config (input type, format, dimensions).
 * @return True if configuration successful, false otherwise.
 */
[[nodiscard]] bool configure_bypass_path_elements(bypass_path_elements &elements, const encoder_config &data);

/**
 * @brief Adds bypass path elements to a pipeline bin.
 *
 * @param pipeline The GstBin/GstPipeline to add elements to.
 * @param elements The elements to add.
 * @return True if all elements added successfully, false otherwise.
 */
[[nodiscard]] bool add_bypass_path_to_pipeline(GstElement *pipeline, bypass_path_elements &elements);

/**
 * @brief Links the bypass path elements together.
 *
 * Links: valve_bypass -> queue_bypass -> nvvidconv_bypass -> appsink_bypass
 *
 * @param elements The elements to link.
 * @return True if linking successful, false otherwise.
 */
[[nodiscard]] bool link_bypass_path_elements(bypass_path_elements &elements);

/**
 * @brief Links the tee element to the bypass path.
 *
 * Requests a new src pad from the tee and links it to valve_bypass.
 *
 * @param tee The tee element to link from.
 * @param elements The bypass path elements.
 * @return True if linking successful, false otherwise.
 */
[[nodiscard]] bool link_tee_to_bypass_path(GstElement *tee, bypass_path_elements &elements);
