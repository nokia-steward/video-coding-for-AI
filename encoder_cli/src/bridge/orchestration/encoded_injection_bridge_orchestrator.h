// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"
#include "../context/encoded_injection_bridge_context.h"
#include "../core/encoded_injection_bridge_core.h"
#include "encoded_bridge_orchestrator.h"

/**
 * @file encoded_injection_bridge_orchestrator.h
 * @brief Orchestrates encoded SEI/EOI frame handling with egress bridge policy.
 */

/**
 * @brief Processes one encoded frame through metadata-aware injection policy.
 */
[[nodiscard]] encoded_bridge_step_result
encoded_injection_process_frame_via_orchestrator(encoded_injection_bridge_context &context, GstBuffer *input_buffer,
                                                 const encoded_frame_luma_adaptation_state &luma_state,
                                                 uint64_t contract_sequence_id);

/**
 * @brief Sends EOS to SEI appsrc for encoded injection path.
 */
[[nodiscard]] encoded_bridge_step_result
encoded_injection_signal_eos_via_orchestrator(encoded_injection_bridge_context &context);
