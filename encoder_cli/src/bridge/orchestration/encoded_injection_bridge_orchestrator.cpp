// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoded_injection_bridge_orchestrator.h"

#include "../../../../libvcai/include/logging.h"
#include "../../encoder_cli_debug.h"
#include "../app/encoder_app_adapter.h"
#include "../app/pipeline_app_adapter.h"
#include "../context/encoded_injection_transform_deps.h"
#include "../context/encoder_bridge_context.h"

namespace {
    [[nodiscard]] encoded_bridge_context make_egress_context(encoded_injection_bridge_context &context) {
        return encoded_bridge_context{.encoding_src = context.ports->encoding_src,
                                      .sei_src = context.ports->sei_src,
                                      .runtime = context.egress_runtime};
    }

    [[nodiscard]] encoded_bridge_push_options make_sei_push_options(const bool count_sei_injection) {
        return {
                .update_output_counter = true,
                .update_sei_injection_counter = count_sei_injection,
                .update_encoder_submission_counter = false,
                .rollback_pending_luma_on_failure = false,
        };
    }
} // namespace

encoded_bridge_step_result
encoded_injection_process_frame_via_orchestrator(encoded_injection_bridge_context &context, GstBuffer *input_buffer,
                                                 const encoded_frame_luma_adaptation_state &luma_state,
                                                 const uint64_t contract_sequence_id) {
    auto &metadata_runtime = *context.metadata_runtime;
    auto &packed_regions_queue = *context.packed_regions_queue;
    auto &memory_runtime = *context.memory_runtime;
    const auto &params = *context.params;
    const encoder_app_adapter egress_app_adapter(*context.host);
    const pipeline_app_adapter app_adapter(*context.host);
    auto egress_context = make_egress_context(context);

    if (contract_sequence_id == 0) {
        metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("encoded frame item has invalid contract sequence id");
        return {.status = encoded_bridge_step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
    }
    if (!metadata_runtime.validate_and_advance(metadata_runtime.expected_encoded_sequence, contract_sequence_id)) {
        app_adapter.request_metadata_contract_fail_stop("encoded frame contract sequence mismatch");
        return {.status = encoded_bridge_step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
    }

    const auto metadata_item_opt = packed_regions_queue.pop();
    if (!metadata_item_opt.has_value()) {
        metadata_runtime.record_validation_failure();
        app_adapter.request_metadata_contract_fail_stop("missing metadata bundle for encoded frame");
        return {.status = encoded_bridge_step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
    }
    metadata_runtime.record_bundle_dequeued();

    const auto &metadata_bundle = *metadata_item_opt;
    if (metadata_bundle.sequence_id != contract_sequence_id) {
        app_adapter.request_metadata_contract_fail_stop("encoded metadata bundle sequence mismatch");
        return {.status = encoded_bridge_step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
    }

    if (context.frame_counter < context.frame_skip) {
        const GstClockTime output_pts = GST_BUFFER_PTS(input_buffer);
        metadata_runtime.register_output_sequence(output_pts, contract_sequence_id);
        const auto push_result = encoded_bridge_push_buffer_ref_via_orchestrator(
                egress_context, egress_app_adapter, encoded_bridge_target::sei_src, input_buffer,
                make_sei_push_options(false));
        if (push_result.status != encoded_bridge_step_status::ok || push_result.flow_return != GST_FLOW_OK) {
            (void) metadata_runtime.unregister_output_sequence(output_pts, contract_sequence_id);
        }
        context.frame_counter++;
        return push_result;
    }

    const auto &region_info = metadata_bundle.regions;
    const encoded_injection_transform_deps injection_deps{.memory_runtime = memory_runtime, .params = params};
    auto transform_result = !region_info.cancel_flag ? encoded_injection_bridge_core::transform_with_metadata(
                                                               injection_deps, input_buffer, region_info, luma_state)
                                                     : encoded_injection_bridge_core::transform_without_metadata(
                                                               injection_deps, input_buffer, luma_state);

    if (transform_result.status == encoded_injection_bridge_core::step_status::fatal_stop) {
        context.frame_counter++;
        return {.status = encoded_bridge_step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
    }

    encoded_bridge_step_result push_result{};
    GstBuffer *pushed_buffer = input_buffer;
    GstClockTime output_pts = GST_BUFFER_PTS(input_buffer);
    if (!transform_result.use_input_buffer_ref && transform_result.owned_buffer != nullptr) {
        pushed_buffer = transform_result.owned_buffer.get();
        output_pts = GST_BUFFER_PTS(pushed_buffer);
        metadata_runtime.register_output_sequence(output_pts, contract_sequence_id);
        push_result = encoded_bridge_push_owned_buffer_via_orchestrator(
                egress_context, egress_app_adapter, encoded_bridge_target::sei_src,
                std::move(transform_result.owned_buffer), make_sei_push_options(transform_result.count_sei_injection));
    } else {
        metadata_runtime.register_output_sequence(output_pts, contract_sequence_id);
        push_result = encoded_bridge_push_buffer_ref_via_orchestrator(
                egress_context, egress_app_adapter, encoded_bridge_target::sei_src, input_buffer,
                make_sei_push_options(transform_result.count_sei_injection));
    }

    if (push_result.status != encoded_bridge_step_status::ok || push_result.flow_return != GST_FLOW_OK) {
        (void) metadata_runtime.unregister_output_sequence(output_pts, contract_sequence_id);
    }

    context.frame_counter++;
    return push_result;
}

encoded_bridge_step_result encoded_injection_signal_eos_via_orchestrator(encoded_injection_bridge_context &context) {
    const encoder_app_adapter app_adapter(*context.host);
    auto egress_context = make_egress_context(context);
    return encoded_bridge_signal_eos_via_orchestrator(egress_context, app_adapter, encoded_bridge_target::sei_src);
}
