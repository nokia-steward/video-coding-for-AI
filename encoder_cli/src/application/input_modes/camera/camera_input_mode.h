// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../input_mode_interface.h"

/**
 * @file camera_input_mode.height
 * @brief Camera input mode implementation.
 */

/**
 * @brief Creates function table for camera input mode.
 * @return Function table for camera input mode.
 */
input_mode_functions create_camera_input_mode_functions();

/**
 * @brief Creates input mode context for camera input mode.
 * @return Input mode context.
 */
input_mode_context create_camera_input_mode_context();
