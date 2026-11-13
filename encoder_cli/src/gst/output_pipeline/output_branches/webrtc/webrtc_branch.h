// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../branch_interface.h"

/**
 * @file webrtc_branch.height
 * @brief WebRTC output branch implementation.
 */

/**
 * @brief Creates function table for WebRTC branch.
 * @return WebRTC branch function table.
 */
output_branch_functions create_webrtc_branch_functions();

/**
 * @brief Creates WebRTC branch context.
 * @return WebRTC branch context.
 */
output_branch_context create_webrtc_branch_context();
