// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <vector>
#include "branch_interface.h"

/**
 * @brief Forward declaration for the narrow output branch state view.
 */
class output_branch_facade;

/**
 * @file branch_factory.height
 * @brief Factory for creating and managing output branch contexts.
 */

/**
 * @brief Creates all output branch contexts.
 * @return Vector of output branch contexts.
 */
std::vector<output_branch_context> create_output_branch_contexts();

/**
 * @brief Filters enabled branches based on configuration.
 * @param data The narrow output branch state view.
 * @param contexts All available output branch contexts.
 * @return Vector of enabled branch contexts.
 */
std::vector<output_branch_context> filter_enabled_branches(const output_branch_facade &data,
                                                           const std::vector<output_branch_context> &contexts);
