// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file state_management.height
 * @brief Declares functions for GStreamer pipeline state management.
 */

/**
 * @brief Start a pipeline and wait for state change, checking for errors.
 * @param pipeline The pipeline element to start.
 * @param pipeline_name Name of the pipeline for error messages.
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool start_pipeline_with_error_check(GstElement *pipeline, const char *pipeline_name);
