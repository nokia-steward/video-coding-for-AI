// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

/**
 * @file rtp_branch.height
 * @brief Backward-compatible WebRTC negotiation API for startup flow.
 */

struct global_data;

/**
 * @brief Validates that WebRTC negotiation can proceed.
 *
 * Negotiation itself is handled by the modular output branch implementation
 * (`output_branches/webrtc`). This function remains as a lightweight
 * compatibility hook for startup code that still invokes it after pipeline
 * transition to PLAYING.
 *
 * @param data Global application state.
 * @return true when WebRTC is not active or when required runtime state exists.
 */
[[nodiscard]] bool trigger_webrtc_negotiation(const global_data &data);
