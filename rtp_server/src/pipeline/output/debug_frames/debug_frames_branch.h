#pragma once

#include "../branch_interface.h"

/**
 * @brief Creates output branch functions for debug PNG frame saving.
 * @return Branch function table.
 */
output_branch_functions create_debug_frames_branch_functions();

/**
 * @brief Creates context for the debug PNG frame branch.
 * @return Branch context.
 */
output_branch_context create_debug_frames_branch_context();
