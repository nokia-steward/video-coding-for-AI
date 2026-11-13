// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../global_data.h"

/**
 * @brief Non-owning view over the output branch slice of global_data.
 */
class output_branch_facade;

/**
 * @file output_pipeline.height
 * @brief Header for output pipeline creation and management functions.
 */

/**
 * @brief Initializes the output pipeline debug category.
 */
void init_output_pipeline_debug_category();

/**
 * @brief Creates the output pipeline.
 * @param data The global data structure.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_output_pipeline(global_data &data);

/**
 * @brief Sets up pad probes on the output pipeline to track frames actually processed.
 *
 * GStreamer's gst_element_get_static_pad and gst_pad_add_probe require non-const pointers,
 * so this function cannot take elements as const even though it only attaches probes.
 *
 * @param data The narrow output branch state view.
 * @return true if pad probes were successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_output_pipeline_probes(output_branch_facade &data);
