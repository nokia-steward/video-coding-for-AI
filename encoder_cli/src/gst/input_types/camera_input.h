// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../encoder_config.h"

struct global_data;

/**
 * @file camera_input.h
 * @brief Camera input pipeline creation and management.
 */

/**
 * @brief Initializes the camera input debug category.
 */
void init_camera_input_debug_category();

/**
 * @brief Creates camera-specific elements for camera input pipelines.
 * @param data The encoder config.
 * @param v4l2src Output parameter for the v4l2src element.
 * @param queue Output parameter for the queue element.
 * @param capsfilter Output parameter for the capsfilter element.
 * @param tee Output parameter for the tee element.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_camera_input_elements(const encoder_config &data, GstElement *&v4l2src, GstElement *&queue,
                                                GstElement *&capsfilter, GstElement *&tee);

/**
 * @brief Creates a camera input pipeline.
 * @param data The global data structure.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_camera_input_pipeline(global_data &data);
