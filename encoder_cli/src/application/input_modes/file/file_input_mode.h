// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../input_mode_interface.h"

/**
 * @file file_input_mode.height
 * @brief File input mode implementation.
 */

/**
 * @brief Creates function table for file input mode.
 * @return Function table for file input mode.
 */
input_mode_functions create_file_input_mode_functions();

/**
 * @brief Creates input mode context for file input mode.
 * @return Input mode context.
 */
input_mode_context create_file_input_mode_context();
