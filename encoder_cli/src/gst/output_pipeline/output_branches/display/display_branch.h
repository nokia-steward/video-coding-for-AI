// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../branch_interface.h"

/**
 * @file display_branch.height
 * @brief Display output branch implementation.
 */

/**
 * @brief Creates function table for display branch.
 * @return Display branch function table.
 */
output_branch_functions create_display_branch_functions();

/**
 * @brief Creates display branch context.
 * @return Display branch context.
 */
output_branch_context create_display_branch_context();
