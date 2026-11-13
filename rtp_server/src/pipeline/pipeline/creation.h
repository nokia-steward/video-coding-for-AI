// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../pipeline.h"

// Forward declaration
struct rtp_pipeline;

/**
 * @brief Create all GStreamer elements for the main pipeline.
 * @param pipeline The pipeline to create elements for.
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool create_elements(rtp_pipeline &pipeline);

/**
 * @brief Configure element properties.
 * @param pipeline The pipeline to configure elements for.
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool configure_elements(rtp_pipeline &pipeline);

/**
 * @brief Setup webrtcbin signals for WebRTC transport.
 * @param context The transport build context to setup signals for.
 * @param signaling_server The signaling server to send messages through.
 * @param client_id The client ID for this pipeline.
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool setup_webrtcbin_signals(transport_build_context &context,
                                           sf::communication::signaling_server *signaling_server,
                                           const std::string &client_id);

/**
 * @brief Add all elements to the main pipeline bin.
 * @param pipeline The pipeline to add elements to.
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool add_elements_to_pipelines(rtp_pipeline &pipeline);
