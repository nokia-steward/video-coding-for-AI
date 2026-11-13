// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <glib.h>
#include "../core/rtp_config.h"

/**
 * @brief RTP statistics monitoring and tracking.
 *
 * This class provides comprehensive RTP statistics monitoring including
 * packet loss, jitter, timing, and quality metrics.
 */
struct rtp_statistics_monitor {
    /**
     * @brief Constructor.
     * @param p The port number for this session.
     * @param cfg Monitoring configuration parameters.
     */
    explicit rtp_statistics_monitor(guint16 p, const rtp_monitoring_config &cfg);

    /**
     * @brief Destructor.
     */
    ~rtp_statistics_monitor() = default;

    // Disable copy constructor and assignment operator
    rtp_statistics_monitor(const rtp_statistics_monitor &) = delete;
    rtp_statistics_monitor &operator=(const rtp_statistics_monitor &) = delete;

    /**
     * @brief Update statistics when a packet is received.
     * @param packet_size Size of the received packet in bytes.
     * @param timestamp RTP timestamp of the packet.
     * @param sequence_number RTP sequence number.
     */
    void on_packet_received(guint32 packet_size, guint32 timestamp, guint16 sequence_number);

    /**
     * @brief Update statistics when a packet is received but RTP metadata is unavailable.
     * @param packet_size Size of the received packet in bytes.
     */
    void on_packet_received_without_metadata(guint32 packet_size);

    /**
     * @brief Update statistics when a frame is decoded.
     * @param frame_size Size of the decoded frame in bytes.
     */
    void on_frame_decoded(guint32 frame_size);

    /**
     * @brief Update statistics when a corrupted frame is detected.
     */
    void on_corrupted_frame();

    /**
     * @brief Update statistics when a packet is lost.
     * @param expected_sequence Expected sequence number.
     * @param actual_sequence Actual sequence number received.
     */
    void on_packet_lost(guint16 expected_sequence, guint16 actual_sequence);

    /**
     * @brief Log current statistics.
     */
    void log_statistics() const;

    /**
     * @brief Get packet loss rate as percentage.
     * @return Packet loss rate (0.0 to 100.0).
     */
    [[nodiscard]] double get_packet_loss_rate() const;

    /**
     * @brief Get average jitter in milliseconds.
     * @return Average jitter.
     */
    [[nodiscard]] double get_average_jitter_ms() const;

    /**
     * @brief Get session duration in milliseconds.
     * @return Session duration.
     */
    [[nodiscard]] guint64 get_session_duration_ms() const;

    /**
     * @brief Check if session should be terminated due to poor quality.
     * Uses configuration parameters to determine which checks to perform.
     * @return true if session should be terminated.
     */
    [[nodiscard]] bool should_terminate_session() const;

    /**
     * @brief Reset all statistics.
     */
    void reset();

private:
    guint16 port;

    // Packet statistics
    std::atomic<guint64> packets_received{0};
    std::atomic<guint64> packets_lost{0};
    std::atomic<guint64> bytes_received{0};
    std::atomic<guint64> frames_decoded{0};
    std::atomic<guint64> corrupted_frames{0};
    // Timing statistics
    std::atomic<gint64> session_start_time{0};
    std::atomic<gint64> last_packet_time{0};
    std::atomic<guint32> last_timestamp{0};
    std::atomic<guint16> last_sequence{0};
    std::atomic<guint16> expected_sequence{0};

    // Jitter calculation
    std::atomic<double> jitter_sum{0.0};
    std::atomic<guint64> jitter_samples{0};

    // Monitoring configuration
    rtp_monitoring_config config;
};

/**
 * @brief Initialize RTP statistics debug category.
 */
void init_rtp_statistics_debug_category();
