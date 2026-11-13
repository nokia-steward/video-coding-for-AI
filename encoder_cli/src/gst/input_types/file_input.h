// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

struct global_data;

/**
 * @file file_input.h
 * @brief File input pipeline creation and management.
 */

/**
 * @brief Initializes the file input debug category.
 */
void init_file_input_debug_category();

/**
 * @brief Creates a file input pipeline.
 * @param data The global data structure.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_file_input_pipeline(global_data &data);
