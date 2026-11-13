#pragma once

#include "../branch_interface.h"

/**
 * @brief Initialize display branch debug category.
 */
void init_display_branch_debug_category();

/**
 * @brief Create display branch function table.
 * @return Display branch functions.
 */
output_branch_functions create_display_branch_functions();

/**
 * @brief Create display branch context.
 * @return Display branch context.
 */
output_branch_context create_display_branch_context();
