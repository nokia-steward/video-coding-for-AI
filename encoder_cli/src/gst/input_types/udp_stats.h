// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>
#include <gst/gst.h>

#include "frame_drop_manager.h"
#include "input_ingress_facade.h"

/**
 * @file udp_stats.h
 * @brief Read-only statistics accessor surface for UDP ingress.
 */

/**
 * @brief Initializes the UDP statistics debug category.
 */
void init_udp_stats_debug_category();

/**
 * @brief Checks if a UDP client has connected and started sending data.
 * @param data Encoder instance owning UDP runtime counters.
 * @return true if at least one UDP packet has been received, false otherwise.
 */
[[nodiscard]] bool has_udp_client_connected(const input_ingress_facade &data);

/**
 * @brief Gets the time when the last UDP packet was received.
 * @param data Encoder instance owning UDP runtime counters.
 * @return Time point of last packet reception, or steady_clock::time_point::min() if no packets received.
 */
[[nodiscard]] std::chrono::steady_clock::time_point get_last_udp_packet_time(const input_ingress_facade &data);

/**
 * @brief Resets UDP connection packet counters to allow waiting for a new client.
 * @param data Encoder instance whose UDP packet stats are reset.
 */
void reset_udp_connection_state(input_ingress_facade &data);

/**
 * @brief Gets the total number of UDP packets received.
 * @param data Encoder instance owning UDP runtime counters.
 * @return Total packet count since last reset.
 */
[[nodiscard]] guint64 get_udp_packet_count(const input_ingress_facade &data);

/**
 * @brief Gets the total number of bytes received via UDP.
 * @param data Encoder instance owning UDP runtime counters.
 * @return Total bytes received since last reset.
 */
[[nodiscard]] guint64 get_udp_bytes_received(const input_ingress_facade &data);

/**
 * @brief Gets the minimum UDP packet size observed.
 * @param data Encoder instance owning UDP runtime counters.
 * @return Minimum packet size in bytes, or 0 if no packets received.
 */
[[nodiscard]] guint64 get_udp_min_packet_size(const input_ingress_facade &data);

/**
 * @brief Gets the maximum UDP packet size observed.
 * @param data Encoder instance owning UDP runtime counters.
 * @return Maximum packet size in bytes, or 0 if no packets received.
 */
[[nodiscard]] guint64 get_udp_max_packet_size(const input_ingress_facade &data);

/**
 * @brief Gets the average UDP packet size.
 * @param data Encoder instance owning UDP runtime counters.
 * @return Average packet size in bytes, or 0.0 if no packets received.
 */
[[nodiscard]] double get_udp_average_packet_size(const input_ingress_facade &data);

/**
 * @brief Gets frame drop statistics from the instance frame drop manager.
 * @param data Encoder instance owning the UDP frame drop manager.
 * @return Statistics struct containing dropped frames count and related metrics.
 */
[[nodiscard]] frame_drop_manager::statistics get_frame_drop_statistics(const input_ingress_facade &data);
