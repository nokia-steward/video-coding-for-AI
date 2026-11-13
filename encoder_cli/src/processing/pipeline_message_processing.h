// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../global_data.h"
#include "gst_raii.h"

/**
 * @file pipeline_message_processing.h
 * @brief Declares the main message processing loop for a GStreamer pipeline.
 */

/**
 * @brief The main function for a pipeline's message bus processing thread.
 *
 * This function runs in a loop, listening on a pipeline's GstBus for
 * ERROR or EOS (End-Of-Stream) messages and updating the shared application
 * state accordingly.
 *
 * @param pipeline The GStreamer pipeline whose bus should be monitored.
 * @param data A reference to the shared global_data struct.
 */
void process_pipeline_messages(const sf::unique_gst_element_ptr &pipeline, global_data &data);

/**
 * @brief Starts a GstTask that polls a pipeline bus (same pattern as bypass workers).
 */
[[nodiscard]] bool start_pipeline_message_gst_task(global_data &data, GstElement *pipeline,
                                                   bypass_gst_poll_slot &slot);

/**
 * @brief Stops and joins a pipeline message GstTask.
 */
void join_pipeline_message_gst_task(bypass_gst_poll_slot &slot);

/**
 * @brief Initializes the GStreamer debug category for pipeline message processing.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_pipeline_message_processing_debug_category();
