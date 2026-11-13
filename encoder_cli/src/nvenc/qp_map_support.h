// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <string>

#include "../encoder_config.h"

/**
 * @file qp_map_support.height
 * @brief Runtime probing and mode resolution for NVENC QP-map delta mode.
 */

/**
 * @brief Result of probing runtime support for QP-map delta mode.
 */
struct qp_map_support_probe_result {
    bool supported = false;
    int detected_ctb_size = 64;
    std::string reason{};
};

/**
 * @brief Probes whether QP-map delta mode can be used with the active encoder.
 * @param encoder Active encoder element instance.
 * @return Probe result including support state and detected CTB size.
 */
[[nodiscard]] qp_map_support_probe_result probe_qp_map_delta_support(GstElement *encoder);

/**
 * @brief Resolves and stores effective background mode at runtime.
 *
 * If requested mode is qp_map_delta and runtime probe fails, this function
 * automatically falls back to background_blur and records the reason.
 *
 * @param config Mutable runtime encoder configuration.
 * @param encoder Active encoder element instance.
 */
void resolve_effective_background_mode(encoder_config &config, GstElement *encoder);
