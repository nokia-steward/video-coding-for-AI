// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../global_data.h"

/**
 * @file rtp_heartbeat.h
 * @brief Header for RTP heartbeat functionality.
 */

/**
 * @brief Initializes the RTP heartbeat debug category.
 */
void init_rtp_heartbeat_debug_category();

/**
 * @brief Starts the heartbeat thread for RTP streaming.
 * @param data Reference to global_data containing RTP configuration.
 * @return true if heartbeat thread started successfully, false otherwise.
 */
[[nodiscard]] bool start_rtp_heartbeat(global_data &data);

/**
 * @brief Joins the heartbeat thread if one was started, then clears `rtp_heartbeat_thread` and resets the started flag.
 */
void join_rtp_heartbeat_if_running(global_data &data);
