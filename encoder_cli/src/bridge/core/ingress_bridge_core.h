// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"
#include "../context/ingress_bridge_context.h"

/**
 * @file ingress_bridge_core.h
 * @brief Media-only ingress bridge operations for appsink pull/EOS checks.
 */

/**
 * @brief Step status used by ingress orchestrators.
 */
enum class ingress_bridge_step_status { ok, recoverable_drop, fatal_stop, drain_complete };

/**
 * @brief Result of one ingress pull step.
 */
struct ingress_bridge_pull_result {
    ingress_bridge_step_status status = ingress_bridge_step_status::ok;
    sf::unique_gst_sample_ptr sample{};
};

/**
 * @brief Pulls one sample from a signal-emitting appsink.
 * @param sink Appsink element connected via new-sample signal.
 * @return Pull result with sample when available.
 */
[[nodiscard]] ingress_bridge_pull_result ingress_bridge_pull_sample_from_signal(GstElement *sink);

/**
 * @brief Tries to pull one sample with timeout from an appsink.
 * @param appsink Appsink element.
 * @param timeout Timeout in nanoseconds.
 * @return Pull result with status indicating sample, timeout, EOS, or fatal error.
 */
[[nodiscard]] ingress_bridge_pull_result ingress_bridge_try_pull_sample(GstElement *appsink, GstClockTime timeout);
