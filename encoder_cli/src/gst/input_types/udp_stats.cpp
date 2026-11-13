// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "udp_stats.h"

#include <chrono>
#include <gst/gst.h>

#include "../../../libvcai/include/logging.h"

/**
 * @file udp_stats.cpp
 * @brief Read-only statistics accessor surface for UDP ingress.
 */

/// GStreamer debug category for UDP ingress statistics.
GST_DEBUG_CATEGORY_STATIC(udp_stats);

void init_udp_stats_debug_category() {
    GST_DEBUG_CATEGORY_INIT(udp_stats, "udp-stats", 0, "Read-only UDP ingress statistics");
}

bool has_udp_client_connected(const input_ingress_facade &data) {
    return data.udp_input_runtime.first_udp_packet_received.load();
}

std::chrono::steady_clock::time_point get_last_udp_packet_time(const input_ingress_facade &data) {
    const auto rep = data.udp_input_runtime.last_packet_time_rep.load();
    if (rep == 0) {
        return std::chrono::steady_clock::time_point::min();
    }
    return std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(rep));
}

void reset_udp_connection_state(input_ingress_facade &data) {
    data.udp_input_runtime.reset_connection_packet_stats();
    sf::info(udp_stats, "UDP connection state reset: Ready to accept new client connection");
}

guint64 get_udp_packet_count(const input_ingress_facade &data) {
    return data.udp_input_runtime.udp_packet_count.load();
}

guint64 get_udp_bytes_received(const input_ingress_facade &data) {
    return data.udp_input_runtime.udp_bytes_received.load();
}

guint64 get_udp_min_packet_size(const input_ingress_facade &data) {
    return data.udp_input_runtime.udp_min_packet_size.load();
}

guint64 get_udp_max_packet_size(const input_ingress_facade &data) {
    return data.udp_input_runtime.udp_max_packet_size.load();
}

double get_udp_average_packet_size(const input_ingress_facade &data) {
    const guint64 packet_count = data.udp_input_runtime.udp_packet_count.load();
    const guint64 total_sizes = data.udp_input_runtime.udp_total_packet_sizes.load();
    if (packet_count == 0) {
        return 0.0;
    }
    return static_cast<double>(total_sizes) / static_cast<double>(packet_count);
}

frame_drop_manager::statistics get_frame_drop_statistics(const input_ingress_facade &data) {
    return data.udp_frame_drop_manager.get_statistics();
}
