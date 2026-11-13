// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../transport_interface.h"

/**
 * @file rtp_input_transport.h
 * @brief RTP input transport function implementations.
 */

/**
 * @brief Create RTP input transport function table.
 * @return Transport function table for RTP input.
 */
[[nodiscard]] transport_functions create_rtp_input_transport_functions();

/**
 * @brief Create RTP input transport context.
 * @return Transport context with RTP input functions and user data.
 */
[[nodiscard]] transport_context create_rtp_input_transport_context();
