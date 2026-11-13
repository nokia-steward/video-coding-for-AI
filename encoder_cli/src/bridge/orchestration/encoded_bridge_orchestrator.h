// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"
#include "../app/encoder_app_adapter.h"
#include "../context/encoder_bridge_context.h"
#include "../core/encoded_bridge_core.h"

/**
 * @file encoded_bridge_orchestrator.h
 * @brief Orchestrates core egress bridge steps with app-side concerns.
 */

/**
 * @brief Optional side effects for one push step.
 */
struct encoded_bridge_push_options {
    bool update_output_counter = false;
    bool update_sei_injection_counter = false;
    bool update_encoder_submission_counter = false;
    bool rollback_pending_luma_on_failure = false;
};

/**
 * @brief Pushes an owned buffer via orchestrator policy.
 * @param context Egress bridge context for current encoder instance.
 * @param app_adapter App-side adapter for status/fail-fast behavior.
 * @param target Target appsrc endpoint.
 * @param buffer Owned buffer to push.
 * @param options Optional side effects for successful/failed push handling.
 * @return Step result with status and flow code.
 */
[[nodiscard]] encoded_bridge_step_result encoded_bridge_push_owned_buffer_via_orchestrator(
        encoded_bridge_context &context, const encoder_app_adapter &app_adapter, encoded_bridge_target target,
        sf::unique_gst_buffer_ptr buffer, const encoded_bridge_push_options &options);

/**
 * @brief Pushes a referenced buffer via orchestrator policy.
 * @param context Egress bridge context for current encoder instance.
 * @param app_adapter App-side adapter for status/fail-fast behavior.
 * @param target Target appsrc endpoint.
 * @param buffer Non-owning input buffer reference.
 * @param options Optional side effects for successful/failed push handling.
 * @return Step result with status and flow code.
 */
[[nodiscard]] encoded_bridge_step_result
encoded_bridge_push_buffer_ref_via_orchestrator(encoded_bridge_context &context, const encoder_app_adapter &app_adapter,
                                                encoded_bridge_target target, GstBuffer *buffer,
                                                const encoded_bridge_push_options &options);

/**
 * @brief Sends EOS via orchestrator policy.
 * @param context Egress bridge context for current encoder instance.
 * @param app_adapter App-side adapter for status/fail-fast behavior.
 * @param target Target appsrc endpoint.
 * @return Step result with status and flow code.
 */
[[nodiscard]] encoded_bridge_step_result
encoded_bridge_signal_eos_via_orchestrator(encoded_bridge_context &context, const encoder_app_adapter &app_adapter,
                                           encoded_bridge_target target);
