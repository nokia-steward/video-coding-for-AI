// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../encoder_config.h"
#include "elements.h"

/**
 * @file queues.height
 * @brief Declares functions for configuring output pipeline queues.
 */

/**
 * @brief Configures output branch queues with leaky downstream to prevent blocking.
 * @param elements The pipeline elements.
 * @param config The encoder config (framerate, packer type for the display queue).
 */
void configure_output_queues(const output_pipeline_elements &elements, const encoder_config &config);

/**
 * @brief Configures the display sink element.
 * @param elements The pipeline elements.
 * @param config The encoder config (packer type).
 */
void configure_display_sink(const output_pipeline_elements &elements, const encoder_config &config);
