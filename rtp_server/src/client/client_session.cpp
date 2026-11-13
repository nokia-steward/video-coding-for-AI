// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "client_session.h"

#include <gst/gst.h>

#include "logging.h"

// Debug category for client session lifecycle logging.
GST_DEBUG_CATEGORY_STATIC(client_session_category);
#define GST_CAT_DEFAULT client_session_category

void init_client_session_debug_category() {
    GST_DEBUG_CATEGORY_INIT(client_session_category, "client-session", 0, "Client Session Management");
}

client_session::client_session(const guint16 p, std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines,
                               const rtp_monitoring_config &monitoring_config) :
    port(p),
    connection_time(std::chrono::steady_clock::now()), activity_mutex(), last_activity(connection_time),
    rtp_monitor(std::make_unique<rtp_statistics_monitor>(port, monitoring_config)), pipeline(nullptr) {
    // Get the pipeline from the client_pipelines map
    if (const auto it = client_pipelines.find(p); it != client_pipelines.end()) {
        pipeline = it->second.get();
    }
}

[[nodiscard]] bool client_session::is_session_timeout(const std::chrono::seconds timeout_duration) const {
    std::lock_guard<std::mutex> lock(activity_mutex);
    return std::chrono::steady_clock::now() - last_activity > timeout_duration;
}

void client_session::log_session_info() const {
    const auto now = std::chrono::steady_clock::now();
    const auto connection_duration = std::chrono::duration_cast<std::chrono::seconds>(now - connection_time);

    sf::info(client_session_category, "Client Session Info for Port %u:", port);
    sf::info(client_session_category, "  Connection Time: %ld seconds ago", connection_duration.count());
    sf::info(client_session_category, "  Status: %s", is_active.load() ? "Active" : "Inactive");
    sf::info(client_session_category, "  Should Terminate: %s", should_terminate.load() ? "Yes" : "No");

    // Only log RTP statistics (more comprehensive)
    if (rtp_monitor) {
        rtp_monitor->log_statistics();
    }
}

void client_session::update_rtp_statistics(const guint32 packet_size, const guint32 timestamp,
                                           const guint16 sequence_number) {
    if (rtp_monitor) {
        rtp_monitor->on_packet_received(packet_size, timestamp, sequence_number);
    }
    update_activity();
}

void client_session::update_rtp_statistics_without_metadata(const guint32 packet_size) {
    if (rtp_monitor) {
        rtp_monitor->on_packet_received_without_metadata(packet_size);
    }
    update_activity();
}

void client_session::update_decoded_frame_statistics(const guint32 frame_size) {
    if (rtp_monitor) {
        rtp_monitor->on_frame_decoded(frame_size);
    }
    update_activity();
}

void client_session::update_activity() {
    std::lock_guard<std::mutex> lock(activity_mutex);
    last_activity = std::chrono::steady_clock::now();
}
