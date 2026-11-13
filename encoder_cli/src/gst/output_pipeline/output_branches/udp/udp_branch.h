// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../branch_interface.h"

/**
 * @file udp_branch.height
 * @brief UDP output branch implementation.
 */

/**
 * @brief Creates function table for UDP branch.
 * @return UDP branch function table.
 */
output_branch_functions create_udp_branch_functions();

/**
 * @brief Creates UDP branch context.
 * @return UDP branch context.
 */
output_branch_context create_udp_branch_context();
