// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>
#include <gst/gst.h>

#include "../app/random_access_app_adapter.h"
#include "../context/random_access_bridge_context.h"
#include "../core/random_access_bridge_core.h"

/**
 * @file random_access_bridge_orchestrator.h
 * @brief Orchestrates random-access core steps with app-side fail-fast policy.
 */

/**
 * @brief Resolves random_access_packer for current instance.
 */
[[nodiscard]] random_access_packer *random_access_get_packer_via_orchestrator(random_access_bridge_context &context,
                                                                              const random_access_app_adapter &adapter);

/**
 * @brief Executes one random-access period through orchestrator policy.
 */
[[nodiscard]] random_access_bridge_core::step_result random_access_process_period_via_orchestrator(
        random_access_bridge_context &context, const random_access_app_adapter &adapter,
        const random_access_packer::complete_period &period, GstClockTime frame_duration, GstClockTime &next_pts,
        std::chrono::steady_clock::time_point &last_push_time);

/**
 * @brief Executes random-access cleanup through orchestrator policy.
 */
[[nodiscard]] random_access_bridge_core::step_result
random_access_cleanup_via_orchestrator(random_access_bridge_context &context, const random_access_app_adapter &adapter);
