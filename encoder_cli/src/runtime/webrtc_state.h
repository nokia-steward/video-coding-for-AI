// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>

/**
 * @file webrtc_state.h
 * @brief Groups WebRTC connection synchronization state.
 */

/**
 * @brief Groups WebRTC connection synchronization state.
 */
struct webrtc_state {
    webrtc_state() : connection_ready(false), connection_mutex(), connection_cv() {}

    std::atomic<bool> connection_ready{false};
    std::mutex connection_mutex;
    std::condition_variable connection_cv;
};
