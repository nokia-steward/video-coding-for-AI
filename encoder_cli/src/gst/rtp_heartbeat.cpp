// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rtp_heartbeat.h"
#include "../../../libvcai/include/communication/manager.h"
#include "../../../libvcai/include/communication/transport_webrtc.h"
#include "../../../libvcai/include/logging.h"
#include "../global_data.h"

#include <gio/gio.h>
#include <gst/gst.h>
#include <thread>

/**
 * @file rtp_heartbeat.cpp
 * @brief Implementation of RTP heartbeat functionality.
 */

/// GStreamer debug category for RTP heartbeat processing.
GST_DEBUG_CATEGORY_STATIC(rtp_heartbeat);
#define GST_CAT_DEFAULT rtp_heartbeat

void init_rtp_heartbeat_debug_category() {
    GST_DEBUG_CATEGORY_INIT(rtp_heartbeat, "rtp-heartbeat", 0, "RTP heartbeat functionality");
}

namespace {
    /**
     * @brief Heartbeat thread function that sends heartbeat messages every 5 seconds.
     * @param data_ptr Pointer to global_data.
     */
    void heartbeat_thread_function(const global_data *data_ptr) {
        sf::info(rtp_heartbeat, "RTP heartbeat thread started");
        sf::info(rtp_heartbeat, "Transport type: %s",
                 (data_ptr->params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc
                          ? "webrtc"
                          : "rtp"));

        // Create communication manager with the same transport type as the main handshake
        sf::info(rtp_heartbeat, "Creating communication manager...");
        const auto manager = sf::communication::create_communication_manager(
                data_ptr->params.communication.resolved_transport_type());
        if (!manager) {
            sf::error(rtp_heartbeat, "Failed to create communication manager for heartbeat");
            return;
        }
        sf::info(rtp_heartbeat, "Communication manager created successfully");

        // Set WebRTC config if needed
        if (data_ptr->params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc) {
            if (data_ptr->params.webrtc.signaling_url.empty()) {
                sf::error(rtp_heartbeat, "WebRTC transport requires signaling_url for heartbeat");
                return;
            }
            sf::info(rtp_heartbeat, "Setting WebRTC config: signaling_url=%s",
                     data_ptr->params.webrtc.signaling_url.c_str());
            sf::communication::webrtc_config webrtc_cfg{};
            webrtc_cfg.signaling_url = data_ptr->params.webrtc.signaling_url;
            webrtc_cfg.ice_timeout_ms = 10000;
            sf::communication::manager_set_webrtc_config(manager.get(), webrtc_cfg);
            sf::info(rtp_heartbeat, "WebRTC config set successfully");
        }

        // Configure transport
        const sf::communication::transport_config config{
                .host = data_ptr->params.communication.host,
                .port = data_ptr->params.communication.port,
                .timeout = std::chrono::milliseconds(1000) // 1 second timeout for heartbeat
        };

        // Initialize and connect
        sf::info(rtp_heartbeat, "Initializing communication manager for heartbeat...");
        const sf::communication::transport_error init_error =
                sf::communication::manager_initialize(manager.get(), config);
        if (init_error != sf::communication::transport_error::success) {
            sf::error(rtp_heartbeat, "Failed to initialize communication manager for heartbeat: %d",
                      static_cast<int>(init_error));
            return;
        }
        sf::info(rtp_heartbeat, "Communication manager initialized successfully");

        // Wait for RTP port to be assigned (with timeout)
        sf::info(rtp_heartbeat, "Waiting for RTP port to be assigned...");
        bool port_assigned = false;
        uint32_t session_id = 0;
        for (int wait_count = 0; wait_count < 100; ++wait_count) {
            const uint16_t assigned_port = data_ptr->assigned_rtp_port.load();
            if (assigned_port > 0) {
                port_assigned = true;
                session_id = data_ptr->assigned_rtp_session_id.load();
                sf::info(rtp_heartbeat, "RTP port %u assigned, starting heartbeat", assigned_port);
                break;
            }
            if (wait_count % 10 == 0) {
                sf::debug(rtp_heartbeat, "Still waiting for RTP port assignment (attempt %d/100)...", wait_count);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (data_ptr->stop_processing.load()) {
                sf::info(rtp_heartbeat, "Stopping heartbeat thread before port assignment");
                [[maybe_unused]] const sf::communication::transport_error shutdown_error =
                        sf::communication::manager_shutdown(manager.get());
                return;
            }
        }

        if (!port_assigned) {
            sf::error(rtp_heartbeat, "RTP port not assigned within 10 seconds, heartbeat thread stopping");
            [[maybe_unused]] const sf::communication::transport_error shutdown_error =
                    sf::communication::manager_shutdown(manager.get());
            return;
        }

        bool first_heartbeat = true;

        while (!data_ptr->stop_processing.load()) {
            // Wait for 5 seconds or until stop is signaled (skip wait for first heartbeat)
            if (!first_heartbeat) {
                for (int i = 0; i < 50 && !data_ptr->stop_processing.load(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                if (data_ptr->stop_processing.load()) {
                    break;
                }
            } else {
                first_heartbeat = false;
            }

            // Check if RTP streaming is still active
            const uint16_t assigned_port = data_ptr->assigned_rtp_port.load();
            if (assigned_port == 0) {
                sf::warning(rtp_heartbeat, "RTP port was reset to 0, stopping heartbeat");
                break;
            }

            // Send heartbeat message using communication manager
            const sf::communication::transport_error error =
                    sf::communication::manager_send_heartbeat(manager.get(), assigned_port, session_id);
            if (error != sf::communication::transport_error::success) {
                sf::warning(rtp_heartbeat, "Error sending heartbeat message: %d", static_cast<int>(error));
            } else {
                sf::info(rtp_heartbeat, "Heartbeat sent for RTP port %u to %s:%u", assigned_port,
                         data_ptr->params.communication.host.c_str(), data_ptr->params.communication.port);
            }
        }

        // Cleanup
        [[maybe_unused]] const sf::communication::transport_error shutdown_error =
                sf::communication::manager_shutdown(manager.get());

        sf::info(rtp_heartbeat, "RTP heartbeat thread stopped");
    }
} // namespace

/**
 * @brief Starts the RTP heartbeat thread if enabled.
 * @param data Global data used for configuration and status.
 * @return True if the heartbeat thread was started.
 */
bool start_rtp_heartbeat(global_data &data) {
    // Only start heartbeat if communication streaming is enabled
    if (data.params.communication.host.empty() || data.params.communication.port == 0) {
        sf::debug(rtp_heartbeat, "Communication streaming not enabled, skipping heartbeat");
        return true; // Not an error, just not needed
    }

    // Prevent multiple heartbeat threads
    bool expected = false;
    if (!data.heartbeat_thread_started.compare_exchange_strong(expected, true)) {
        sf::warning(rtp_heartbeat, "Heartbeat thread already started, skipping");
        return true; // Not an error, just already started
    }

    data.rtp_heartbeat_thread.emplace(heartbeat_thread_function, &data);

    sf::info(rtp_heartbeat, "RTP heartbeat thread started for %s:%u", data.params.communication.host.c_str(),
             data.params.communication.port);
    return true;
}

void join_rtp_heartbeat_if_running(global_data &data) {
    if (!data.rtp_heartbeat_thread.has_value()) {
        return;
    }
    // Ensure the worker observes shutdown (e.g. destructor path without `shutdown()`).
    data.stop_processing.store(true);
    sf::debug(rtp_heartbeat, "Joining RTP heartbeat thread");
    data.rtp_heartbeat_thread->join();
    data.rtp_heartbeat_thread.reset();
    data.heartbeat_thread_started.store(false);
}
