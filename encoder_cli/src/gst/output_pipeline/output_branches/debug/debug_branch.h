// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../branch_interface.h"

/**
 * @file debug_branch.height
 * @brief Debug output branch implementation.
 */

/**
 * @brief Creates function table for debug branch.
 * @return Debug branch function table.
 */
output_branch_functions create_debug_branch_functions();

/**
 * @brief Creates debug branch context.
 * @return Debug branch context.
 */
output_branch_context create_debug_branch_context();
