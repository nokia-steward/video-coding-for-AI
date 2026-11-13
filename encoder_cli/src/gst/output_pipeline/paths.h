// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include "../../encoder_config.h"
#include "elements.h"

/**
 * @file paths.height
 * @brief Declares functions for configuring output file paths.
 */

/**
 * @brief Determines the model name from the detection config file.
 * @return The model name ("yolo", "peoplenet", or "unknown").
 */
[[nodiscard]] std::string determine_model_name();

/**
 * @brief Configures output file paths and creates directories.
 * @param config The encoder config (pack_rois, debug save_frames).
 * @param elements The pipeline elements.
 * @return true if configuration was successful, false otherwise.
 */
[[nodiscard]] bool configure_output_paths(const encoder_config &config, const output_pipeline_elements &elements);
