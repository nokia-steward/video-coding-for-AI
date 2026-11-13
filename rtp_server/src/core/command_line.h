// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "configuration_manager.h"

/**
 * @file command_line.h
 * @brief Defines the structure and parsing logic for command-line parameters.
 */

/**
 * @brief Initialize the command line debug category.
 */
void init_command_line_debug_category();

/**
 * @brief Parses command-line arguments to get all configuration parameters.
 *
 * This function uses GLib's GOptionContext to define and parse command-line arguments.
 * It handles the --config/-c option to specify the configuration file path and optional
 * parameters for all configuration sections using the <group>-<option> naming system.
 * Command line options override values from the configuration file.
 * It will exit the program with an error message if parsing fails.
 *
 * @param argc The argument count from main().
 * @param argv The argument vector from main().
 * @return A struct containing all parsed configuration parameters.
 * @note This function will exit the program on parsing errors.
 */
[[nodiscard]] command_line_parameters parse_command_line(int argc, char **argv);
