// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../global_data.h"
#include "input_mode_interface.h"

#include <vector>

/**
 * @file input_mode_factory.h
 * @brief Factory for creating and selecting input mode contexts.
 */

/**
 * @brief Creates all input mode contexts.
 * @return Vector of input mode contexts.
 */
std::vector<input_mode_context> create_input_mode_contexts();

/**
 * @brief Selects the active input mode based on configuration.
 * @param data The global data structure.
 * @param contexts All available input mode contexts.
 * @return Pointer to active input mode context, or nullptr if none match.
 */
const input_mode_context *select_active_input_mode(const global_data &data,
                                                   const std::vector<input_mode_context> &contexts);
