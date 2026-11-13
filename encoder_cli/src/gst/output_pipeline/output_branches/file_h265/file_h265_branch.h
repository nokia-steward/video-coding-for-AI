// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../branch_interface.h"

/**
 * @file file_h265_branch.height
 * @brief H.265 file output branch implementation.
 */

/**
 * @brief Creates function table for H.265 file branch.
 * @return H.265 file branch function table.
 */
output_branch_functions create_file_h265_branch_functions();

/**
 * @brief Creates H.265 file branch context.
 * @return H.265 file branch context.
 */
output_branch_context create_file_h265_branch_context();
