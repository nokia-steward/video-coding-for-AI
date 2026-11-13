// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../core/rtp_server_config.h"
#include "transport_interface.h"

/**
 * @file transport_factory.h
 * @brief Factory function for creating transport contexts.
 */

/**
 * @brief Create a transport context based on transport type.
 * @param transport_type The transport type.
 * @param config Optional full config (required for WebRTC).
 * @return Transport context, or invalid context on error (empty functions).
 */
[[nodiscard]] transport_context create_transport_context(transport_type transport_type,
                                                         const rtp_server_config *config = nullptr);

/**
 * @brief Free transport context and associated resources.
 * @param context The transport context to free.
 */
void free_transport_context(transport_context *context);
