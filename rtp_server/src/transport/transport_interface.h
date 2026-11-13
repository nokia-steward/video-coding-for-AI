// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <functional>
#include "../core/rtp_config.h"
#include "communication/signaling.h"

struct rtp_server;
struct transport_build_context;

/**
 * @file transport_interface.h
 * @brief Function table for transport implementations using std::function.
 *
 * This file defines the function table structure that all transport
 * implementations must provide. It uses functional programming principles:
 * - No classes or inheritance
 * - Pure functions where possible
 * - Data structures separate from behavior
 * - Modern C++20 std::function instead of C-style function pointers
 */

/**
 * @brief Function table for transport implementations.
 *
 * This struct contains std::function objects that define the contract
 * all transport implementations must fulfill. It separates transport-specific
 * logic from the core server using functional programming principles.
 *
 * Uses std::function to allow lambdas with captures, eliminating the need
 * for void* user_data parameters.
 */
struct transport_functions {
    /**
     * @brief Initialize transport-specific server components.
     * @param server The RTP server instance.
     * @return true if successful, false otherwise.
     */
    std::function<bool(rtp_server *)> initialize_server;

    /**
     * @brief Create transport-specific input pipeline elements.
     * @param pipeline The pipeline to populate.
     * @return true if successful, false otherwise.
     */
    std::function<bool(transport_build_context *)> create_input_elements;

    /**
     * @brief Configure transport-specific pipeline elements.
     * @param pipeline The pipeline to configure.
     * @return true if successful, false otherwise.
     */
    std::function<bool(transport_build_context *)> configure_input_elements;

    /**
     * @brief Link transport-specific input pipeline elements.
     * @param pipeline The pipeline to link.
     * @return true if successful, false otherwise.
     */
    std::function<bool(transport_build_context *)> link_input_pipeline;

    /**
     * @brief Setup transport-specific signal handlers.
     * @param pipeline The pipeline.
     * @param signaling_server Signaling server (for WebRTC, nullptr for RTP input).
     * @param client_id Client ID (for WebRTC, empty for RTP input).
     * @return true if successful, false otherwise.
     */
    std::function<bool(transport_build_context *, sf::communication::signaling_server *, const std::string *)>
            setup_signals;

    /**
     * @brief Handle transport-specific signaling messages.
     * @param pipeline The pipeline.
     * @param msg The signaling message.
     * @param client_id Client ID.
     * @param signaling_server Signaling server.
     */
    std::function<void(transport_build_context *, const sf::communication::signaling_message *, const std::string *,
                       sf::communication::signaling_server *)>
            handle_signaling_message;

    /**
     * @brief Cleanup transport-specific resources.
     * @param pipeline The pipeline.
     */
    std::function<void(transport_build_context *)> cleanup;

    /**
     * @brief Get transport type.
     * @return The transport type enum.
     */
    std::function<transport_type()> get_transport_type;
};

/**
 * @brief Transport context containing function table.
 *
 * No user_data needed - lambdas can capture any needed state.
 */
struct transport_context {
    transport_functions functions;
};
