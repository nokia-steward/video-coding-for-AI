// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rtp_statistics.h"

#include <cmath>
#include <glib.h>
#include <gst/gst.h>

#include "logging.h"

// Debug category for RTP statistics monitoring.
GST_DEBUG_CATEGORY_STATIC(rtp_statistics_category);
#define GST_CAT_DEFAULT rtp_statistics_category

/**
 * @brief Initialize RTP statistics debug category.
 */
void init_rtp_statistics_debug_category() {
    GST_DEBUG_CATEGORY_INIT(rtp_statistics_category, "rtp-statistics", 0, "RTP Statistics Monitoring");
}

rtp_statistics_monitor::rtp_statistics_monitor(const guint16 p, const rtp_monitoring_config &cfg) :
    port(p), config(cfg) {
    session_start_time = g_get_monotonic_time();
    last_packet_time.store(session_start_time.load());
    expected_sequence = 0;
    last_sequence = 0;
    last_timestamp = 0;

    sf::info(rtp_statistics_category, "RTP statistics monitoring started for port %u", port);
}

void rtp_statistics_monitor::on_packet_received(const guint32 packet_size, const guint32 timestamp,
                                                const guint16 sequence_number) {
    ++packets_received;
    bytes_received += packet_size;
    last_packet_time = g_get_monotonic_time();

    // Calculate jitter (simplified calculation)
    if (last_timestamp.load() != 0) {
        const auto timestamp_diff = static_cast<double>(timestamp - last_timestamp.load());
        const auto time_diff_ms = static_cast<double>(last_packet_time.load() - session_start_time.load()) / 1000.0;
        const auto expected_diff = time_diff_ms * 90.0; // Assuming 90kHz clock rate
        const auto jitter = std::abs(timestamp_diff - expected_diff);

        jitter_sum += jitter;
        ++jitter_samples;
    }

    last_timestamp = timestamp;

    // Check for packet loss
    if (last_sequence.load() != 0) {
        if (const auto expected_seq = (last_sequence.load() + 1) % 65536; sequence_number != expected_seq) {
            const auto lost_packets = static_cast<guint64>((sequence_number - expected_seq + 65536) % 65536);
            packets_lost += lost_packets;
            sf::warning(rtp_statistics_category,
                        "Packet loss detected on port %u: expected %u, got %u (lost %lu packets)", port, expected_seq,
                        sequence_number, lost_packets);
        }
    }

    last_sequence = sequence_number;
    expected_sequence = static_cast<guint16>((sequence_number + 1) % 65536);

    sf::debug(rtp_statistics_category, "Packet received on port %u: seq=%u, size=%u, timestamp=%u", port,
              sequence_number, packet_size, timestamp);
}

void rtp_statistics_monitor::on_packet_received_without_metadata(const guint32 packet_size) {
    ++packets_received;
    bytes_received += packet_size;
    last_packet_time = g_get_monotonic_time();
    sf::debug(rtp_statistics_category, "Packet received on port %u without RTP metadata: size=%u", port, packet_size);
}

void rtp_statistics_monitor::on_frame_decoded(const guint32 frame_size) {
    ++frames_decoded;
    sf::debug(rtp_statistics_category, "Frame decoded on port %u: size=%u", port, frame_size);
}

void rtp_statistics_monitor::on_corrupted_frame() {
    ++corrupted_frames;
    sf::warning(rtp_statistics_category, "Corrupted frame detected on port %u (total: %lu)", port,
                corrupted_frames.load());
}

void rtp_statistics_monitor::on_packet_lost(const guint16 sequence, const guint16 actual_sequence) {
    const auto lost_packets = static_cast<guint64>((actual_sequence - sequence + 65536) % 65536);
    packets_lost += lost_packets;
    sf::warning(rtp_statistics_category, "Packet loss detected on port %u: expected %u, got %u (lost %lu packets)",
                port, sequence, actual_sequence, lost_packets);
}

void rtp_statistics_monitor::log_statistics() const {
    const auto current_time = g_get_monotonic_time();
    const auto duration_ms = static_cast<guint64>((current_time - session_start_time.load()) / 1000);
    const auto packet_loss_rate = get_packet_loss_rate();
    const auto avg_jitter = get_average_jitter_ms();
    const auto silence_duration = static_cast<guint64>((current_time - last_packet_time.load()) / 1000);

    sf::info(rtp_statistics_category, "RTP Statistics for Port %u:", port);
    sf::info(rtp_statistics_category, "  Session Duration: %lu ms", duration_ms);
    sf::info(rtp_statistics_category, "  Packets Received: %lu", packets_received.load());
    sf::info(rtp_statistics_category, "  Packets Lost: %lu", packets_lost.load());
    sf::info(rtp_statistics_category, "  Packet Loss Rate: %.2f%%", packet_loss_rate);
    sf::info(rtp_statistics_category, "  Bytes Received: %lu", bytes_received.load());
    sf::info(rtp_statistics_category, "  Frames Decoded: %lu", frames_decoded.load());
    sf::info(rtp_statistics_category, "  Corrupted Frames: %lu", corrupted_frames.load());
    sf::info(rtp_statistics_category, "  Average Jitter: %.2f ms", avg_jitter);
    sf::info(rtp_statistics_category, "  Silence Duration: %lu ms", silence_duration);
}

double rtp_statistics_monitor::get_packet_loss_rate() const {
    const auto total_packets = static_cast<double>(packets_received.load() + packets_lost.load());
    if (total_packets == 0)
        return 0.0;
    return static_cast<double>(packets_lost.load()) / total_packets * 100.0;
}

double rtp_statistics_monitor::get_average_jitter_ms() const {
    if (jitter_samples.load() == 0)
        return 0.0;
    return jitter_sum.load() / static_cast<double>(jitter_samples.load()) / 90.0; // Convert to ms (90kHz clock)
}

guint64 rtp_statistics_monitor::get_session_duration_ms() const {
    return static_cast<guint64>((g_get_monotonic_time() - session_start_time.load()) / 1000);
}

bool rtp_statistics_monitor::should_terminate_session() const {
    const auto current_time = g_get_monotonic_time();
    const auto silence_duration_ms = static_cast<guint64>((current_time - last_packet_time.load()) / 1000);
    const auto packet_loss_rate = get_packet_loss_rate();
    const auto avg_jitter = get_average_jitter_ms();

    // Check for silence (no packets received for too long)
    if (config.enable_silence_check && silence_duration_ms > config.max_silence_ms) {
        sf::warning(rtp_statistics_category,
                    "Client on port %u appears disconnected - silence duration: %lu ms (threshold: %lu ms)", port,
                    silence_duration_ms, config.max_silence_ms);
        return true;
    }

    // Check for poor quality (high packet loss)
    if (config.enable_packet_loss_check && packet_loss_rate > config.max_packet_loss_rate) {
        sf::warning(rtp_statistics_category,
                    "Client on port %u has poor quality - packet loss rate: %.2f%% (threshold: %.2f%%)", port,
                    packet_loss_rate, config.max_packet_loss_rate);
        return true;
    }

    // Check for poor quality (high jitter)
    if (config.enable_jitter_check && avg_jitter > config.max_jitter_ms) {
        sf::warning(rtp_statistics_category,
                    "Client on port %u has poor quality - average jitter: %.2f ms (threshold: %.2f ms)", port,
                    avg_jitter, config.max_jitter_ms);
        return true;
    }

    return false;
}

void rtp_statistics_monitor::reset() {
    packets_received = 0;
    packets_lost = 0;
    bytes_received = 0;
    frames_decoded = 0;
    corrupted_frames = 0;
    session_start_time = g_get_monotonic_time();
    last_packet_time.store(session_start_time.load());
    last_timestamp = 0;
    last_sequence = 0;
    expected_sequence = 0;
    jitter_sum = 0.0;
    jitter_samples = 0;
    sf::info(rtp_statistics_category, "RTP statistics reset for port %u", port);
}
