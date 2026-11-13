// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../global_data.h"

/**
 * @file udp_mode.height
 * @brief Declares the function to run the application in UDP mode.
 */

/**
 * @brief Runs the application in UDP mode with connection timeout and reconnection loop.
 * @param data The global data structure.
 */
void run_udp_mode(global_data &data);
