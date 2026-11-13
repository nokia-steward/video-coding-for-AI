// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>

#include "communication/signaling.h"

struct transport_build_context;

/**
 * @brief Handle WebRTC signaling messages (SDP offer / ICE candidate) for one pipeline.
 *
 * @param context Target transport build context.
 * @param msg Signaling message payload.
 * @param client_id Client identifier for response routing.
 * @param signaling_server Signaling server used to send answers.
 */
void handle_webrtc_signaling_message(transport_build_context *context, const sf::communication::signaling_message &msg,
                                     const std::string &client_id,
                                     sf::communication::signaling_server *signaling_server);
