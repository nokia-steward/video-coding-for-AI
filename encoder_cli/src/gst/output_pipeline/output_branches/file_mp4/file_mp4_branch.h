// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../branch_interface.h"

/**
 * @file file_mp4_branch.height
 * @brief MP4 file output branch implementation.
 */

/**
 * @brief Creates function table for MP4 branch.
 * @return MP4 branch function table.
 */
output_branch_functions create_file_mp4_branch_functions();

/**
 * @brief Creates MP4 branch context.
 * @return MP4 branch context.
 */
output_branch_context create_file_mp4_branch_context();
