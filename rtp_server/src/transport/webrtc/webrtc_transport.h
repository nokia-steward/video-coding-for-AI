// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../core/rtp_server_config.h"
#include "../transport_interface.h"

/**
 * @file webrtc_transport.h
 * @brief WebRTC transport function implementations.
 */

/**
 * @brief Create WebRTC transport function table.
 * @param config WebRTC configuration (captured in lambda closures).
 * @return Transport function table for WebRTC.
 */
[[nodiscard]] transport_functions create_webrtc_transport_functions(const rtp_server_config *config);

/**
 * @brief Create WebRTC transport context.
 * @param config WebRTC configuration (must remain valid for lifetime of context).
 * @return Transport context with WebRTC functions and user data.
 */
[[nodiscard]] transport_context create_webrtc_transport_context(const rtp_server_config *config);
