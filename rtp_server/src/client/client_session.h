// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>

#include "../core/rtp_config.h"
#include "../pipeline/pipeline.h"
#include "../utils/rtp_statistics.h"

// Forward declarations
struct rtp_pipeline;

/**
 * @brief Client session information and management.
 *
 * This struct manages all aspects of a client session including
 * connection tracking, RTP monitoring, and pipeline management.
 */
struct client_session {
    guint16 port;
    std::chrono::steady_clock::time_point connection_time;
    mutable std::mutex activity_mutex;
    std::chrono::steady_clock::time_point last_activity;
    std::unique_ptr<rtp_statistics_monitor> rtp_monitor;
    rtp_pipeline *pipeline; // Non-owning pointer - managed by client_manager
    std::atomic<bool> is_active{true};
    std::atomic<bool> should_terminate{false};

    // Disable copy constructor and assignment operator
    client_session(const client_session &) = delete;
    client_session &operator=(const client_session &) = delete;

    /**
     * @brief Constructor for client session.
     * @param p The port number for this session.
     * @param client_pipelines Reference to the client pipelines map.
     * @param monitoring_config RTP monitoring configuration.
     */
    explicit client_session(guint16 p, std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines,
                            const rtp_monitoring_config &monitoring_config);

    /**
     * @brief Check if the session has timed out.
     * @param timeout_duration The timeout duration in seconds (default: 30).
     * @return true if the session has timed out, false otherwise.
     */
    [[nodiscard]] bool is_session_timeout(std::chrono::seconds timeout_duration = std::chrono::seconds(30)) const;

    /**
     * @brief Log comprehensive session information.
     */
    void log_session_info() const;

    /**
     * @brief Update RTP statistics when a packet is received.
     * @param packet_size Size of the received packet in bytes.
     * @param timestamp RTP timestamp of the packet.
     * @param sequence_number RTP sequence number.
     */
    void update_rtp_statistics(guint32 packet_size, guint32 timestamp, guint16 sequence_number);

    /**
     * @brief Update RTP statistics when packet metadata is unavailable.
     * @param packet_size Size of the received packet in bytes.
     */
    void update_rtp_statistics_without_metadata(guint32 packet_size);

    /**
     * @brief Update decoded-frame statistics.
     * @param frame_size Size of the decoded frame in bytes.
     */
    void update_decoded_frame_statistics(guint32 frame_size);

    /**
     * @brief Update last activity timestamp.
     */
    void update_activity();
};

/**
 * @brief Initialize the client session debug category.
 */
void init_client_session_debug_category();
