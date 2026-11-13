// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../../global_data.h"

/**
 * @file fps_overlay.height
 * @brief FPS overlay functionality for display branch.
 */

/**
 * @brief Initializes the FPS overlay debug category.
 */
void init_fps_overlay_debug_category();

/**
 * @brief Sets up FPS overlay drawing on display branch frames.
 *
 * Adds a pad probe on the converter output pad to measure playback FPS
 * and draw it in the top right corner of frames.
 *
 * @param converter The converter element in the display branch.
 * @param data The global data structure containing configuration.
 * @return true if pad probe was successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_fps_overlay(GstElement *converter, const global_data &data);

/**
 * @brief Clears FPS tracking data.
 *
 * @note FPS tracking is now handled by pipeline_fps_monitor in global_data.
 * This function is kept for API compatibility but does nothing.
 */
void clear_fps_tracker();
