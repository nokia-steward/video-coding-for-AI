// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <functional>

#include "../global_data.h"

/**
 * @file input_pipeline.height
 * @brief Header for input pipeline creation and management functions.
 */

/**
 * @brief Initializes the input pipeline debug category.
 */
void init_input_pipeline_debug_category();

/**
 * @brief Creates the main input pipeline based on configuration.
 * @param data The global data structure.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_input_pipeline(global_data &data);

/**
 * @brief Selects the appropriate input pipeline creator function.
 * @param data The global data structure.
 * @return Function pointer to the appropriate pipeline creator.
 */
[[nodiscard]] std::function<bool(global_data &)> select_input_pipeline_creator(const global_data &data);

/**
 * @brief Creates the encoding pipeline.
 * @param data The global data structure.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_encoding_pipeline(global_data &data);
