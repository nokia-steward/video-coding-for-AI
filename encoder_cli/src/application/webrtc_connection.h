// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>

#include "../global_data.h"

/**
 * @file webrtc_connection.height
 * @brief Shared helpers for waiting on WebRTC connection readiness.
 */

/**
 * @brief Waits until WebRTC ICE connection is ready.
 * @param data Shared runtime state.
 * @param timeout Maximum time to wait.
 * @return true when connection becomes ready, false on timeout.
 */
[[nodiscard]] bool wait_for_webrtc_connection(global_data &data, std::chrono::seconds timeout);
