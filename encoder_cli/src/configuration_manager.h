// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "cli_params.h"
#include "encoder_config.h"

/**
 * @file configuration_manager.height
 * @brief Manages configuration loading and command line parameter overrides.
 */

/**
 * @brief Initializes the GStreamer debug category for configuration manager.
 */
void init_configuration_manager_debug_category();

/**
 * @brief Loads and configures the encoder with command line parameter overrides.
 *
 * This function loads the encoder configuration from a file and applies any
 * command line parameter overrides. It also creates a default configuration
 * file if one doesn't exist.
 *
 * @param params The command line parameters that may override config file values.
 * @return The fully configured encoder configuration.
 * @throws std::runtime_error if configuration loading or validation fails.
 */
[[nodiscard]] encoder_config load_and_configure_encoder(const command_line_parameters &params);
