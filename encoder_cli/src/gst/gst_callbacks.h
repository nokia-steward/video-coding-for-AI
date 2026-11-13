// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include "../global_data.h"

/**
 * @file gst_callbacks.height
 * @brief GStreamer signal callback functions.
 */

/**
 * @brief Callback function for the "new-sample" signal of the input appsink.
 * @param sink The appsink element that emitted the signal.
 * @param data A pointer to the global data structure.
 * @return GST_FLOW_OK if the sample was processed successfully, GST_FLOW_ERROR otherwise.
 */
GstFlowReturn on_new_raw_sample(GstElement *sink, global_data *data);

/**
 * @brief Callback function for the "eos" signal of the input appsink.
 * @param sink The appsink element that emitted the signal.
 * @param data A pointer to the global data structure.
 */
void on_raw_eos(GstElement *sink, global_data *data);

/**
 * @brief Callback function for the "new-sample" signal of the encoding appsink.
 * @param sink The appsink element that emitted the signal.
 * @param data A pointer to the global data structure.
 * @return GST_FLOW_OK if the sample was processed successfully, GST_FLOW_ERROR otherwise.
 */
GstFlowReturn on_new_encoded_sample(GstElement *sink, global_data *data);

/**
 * @brief Callback function for the "eos" signal of the encoding appsink.
 * @param sink The appsink element that emitted the signal.
 * @param data A pointer to the global data structure.
 */
void on_encoded_eos(GstElement *sink, global_data *data);

/**
 * @brief Initializes the GStreamer debug category for callbacks.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_gst_callbacks_debug_category();
