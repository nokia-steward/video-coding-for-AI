// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include <vector>

#include "gst/gstelement.h"

/**
 * @file config_parser.height
 * @brief Declares functions for parsing configuration files and extracting class IDs.
 */

/**
 * @brief Initializes the debug category for config parser operations.
 */
void init_config_parser_debug_category();

/**
 * @brief Gets the class IDs for the given labels.
 * @param gie A pointer to the GstNvInfer element.
 * @param labels A vector of strings, where each string is a label.
 * @return A vector of class IDs.
 */
[[nodiscard]] std::vector<size_t> get_class_ids(GstElement *gie, const std::vector<std::string> &labels);
