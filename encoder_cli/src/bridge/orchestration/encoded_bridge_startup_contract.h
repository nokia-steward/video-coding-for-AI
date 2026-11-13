// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>

#include <gst/gst.h>

#include "../context/encoded_stream_ports.h"
#include "../context/encoder_bridge_context.h"
#include "../context/encoder_bridge_runtime.h"

/**
 * @file encoded_bridge_startup_contract.h
 * @brief Shared startup contract for encoder egress appsrc endpoints.
 */

/**
 * @brief Startup contract options for one appsrc endpoint.
 */
struct encoded_bridge_startup_contract_options {
    bool is_live = false;
    bool do_timestamp = false;
    bool block = false;
    std::optional<guint64> max_bytes = std::nullopt;
};

/**
 * @brief Applies startup appsrc properties and establishes one-way caps lock.
 * @param ports Encoder egress appsrc handles.
 * @param egress_runtime Encoder egress bridge lifecycle state.
 * @param target Target endpoint to configure.
 * @param caps Startup caps for this endpoint.
 * @param options Startup property options.
 * @return True when startup contract is established successfully.
 */
[[nodiscard]] bool configure_encoded_bridge_startup_contract(encoded_stream_ports &ports,
                                                             encoded_bridge_runtime &egress_runtime,
                                                             encoded_bridge_target target, GstCaps *caps,
                                                             const encoded_bridge_startup_contract_options &options);
