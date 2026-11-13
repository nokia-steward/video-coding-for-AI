// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "encoder_bridge_runtime.h"

/**
 * @file encoder_bridge_context.h
 * @brief Egress bridge context wiring for encoder appsrc endpoints.
 */

/**
 * @brief Supported appsrc endpoints in the encoder egress path.
 */
enum class encoded_bridge_target { encoding_src, sei_src };

/**
 * @brief Aggregates appsrc pointers and runtime flags for egress operations.
 *
 * Appsrc pointers are non-owning references managed by the GStreamer pipeline.
 */
struct encoded_bridge_context {
    GstElement *encoding_src = nullptr;
    GstElement *sei_src = nullptr;
    encoded_bridge_runtime *runtime = nullptr;
};
