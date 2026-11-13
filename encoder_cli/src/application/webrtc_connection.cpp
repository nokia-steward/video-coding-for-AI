// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "webrtc_connection.h"

#include "../../libvcai/include/gst_raii.h"
#include "../encoder_cli_debug.h"

#include <gst/gst.h>
#include <mutex>

/**
 * @file webrtc_connection.cpp
 * @brief Implementation of shared WebRTC connection wait logic.
 */

bool wait_for_webrtc_connection(global_data &data, const std::chrono::seconds timeout) {
    bool connection_ready = false;
    if (data.output_pipeline) {
        const sf::unique_gst_element_ptr webrtcbin(
                gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "webrtcbin"));
        if (webrtcbin) {
            gint state_int = 0;
            g_object_get(webrtcbin.get(), "ice-connection-state", &state_int, nullptr);
            constexpr gint ice_connection_state_connected = 2;
            constexpr gint ice_connection_state_completed = 3;
            if (state_int == ice_connection_state_connected || state_int == ice_connection_state_completed) {
                connection_ready = true;
                std::lock_guard<std::mutex> lock(data.webrtc.connection_mutex);
                data.webrtc.connection_ready = true;
                sf::info(encoder_cli_main, "WebRTC ICE connection already established (state=%d)", state_int);
            } else {
                sf::info(encoder_cli_main, "WebRTC ICE connection state: %d (waiting for CONNECTED/COMPLETED)",
                         state_int);
            }
        }
    }

    if (!connection_ready) {
        std::unique_lock<std::mutex> lock(data.webrtc.connection_mutex);
        sf::info(encoder_cli_main, "Waiting for WebRTC connection before starting input/encoding pipelines...");
        const bool ready = data.webrtc.connection_cv.wait_for(lock, timeout,
                                                              [&data] { return data.webrtc.connection_ready.load(); });

        if (!ready) {
            sf::error(encoder_cli_main, "Timeout waiting for WebRTC connection (%lld seconds)",
                      static_cast<long long>(timeout.count()));
            return false;
        }
    }

    sf::info(encoder_cli_main, "WebRTC connection ready - starting input/encoding pipelines");
    return true;
}
