// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"
#include "../context/encoder_bridge_context.h"

/**
 * @file encoded_bridge_core.h
 * @brief Media-only egress bridge operations for appsrc push/EOS.
 */

/**
 * @brief Step status used by egress orchestrators.
 */
enum class encoded_bridge_step_status { ok, recoverable_drop, fatal_stop, drain_complete };

/**
 * @brief Result of one egress bridge step.
 */
struct encoded_bridge_step_result {
    encoded_bridge_step_status status = encoded_bridge_step_status::ok;
    GstFlowReturn flow_return = GST_FLOW_OK;
};

/**
 * @brief Pushes an owned buffer to one egress appsrc endpoint.
 * @param context Egress bridge context for current encoder instance.
 * @param target Target appsrc endpoint.
 * @param buffer Owned buffer to push.
 * @return Step result with status and flow code.
 */
[[nodiscard]] encoded_bridge_step_result encoded_bridge_push_owned_buffer(encoded_bridge_context &context,
                                                                          encoded_bridge_target target,
                                                                          sf::unique_gst_buffer_ptr buffer);

/**
 * @brief Pushes a referenced input buffer to one egress appsrc endpoint.
 * @param context Egress bridge context for current encoder instance.
 * @param target Target appsrc endpoint.
 * @param buffer Non-owning input buffer reference.
 * @return Step result with status and flow code.
 */
[[nodiscard]] encoded_bridge_step_result
encoded_bridge_push_buffer_ref(encoded_bridge_context &context, encoded_bridge_target target, GstBuffer *buffer);

/**
 * @brief Sends EOS to one egress appsrc endpoint.
 * @param context Egress bridge context for current encoder instance.
 * @param target Target appsrc endpoint.
 * @return Step result with status and flow code.
 */
[[nodiscard]] encoded_bridge_step_result encoded_bridge_signal_eos(encoded_bridge_context &context,
                                                                   encoded_bridge_target target);
