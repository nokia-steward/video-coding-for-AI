// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../input_mode_interface.h"

/**
 * @file udp_input_mode.height
 * @brief UDP input mode implementation.
 */

/**
 * @brief Creates function table for UDP input mode.
 * @return Function table for UDP input mode.
 */
input_mode_functions create_udp_input_mode_functions();

/**
 * @brief Creates input mode context for UDP input mode.
 * @return Input mode context.
 */
input_mode_context create_udp_input_mode_context();
