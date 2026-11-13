// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoded_bridge_orchestrator.h"

namespace {
    void apply_success_side_effects(const encoder_app_adapter &app_adapter,
                                    const encoded_bridge_push_options &options) {
        if (options.update_encoder_submission_counter) {
            app_adapter.record_encoder_submission();
        }
        if (options.update_sei_injection_counter) {
            app_adapter.record_sei_injection(1);
        }
        if (options.update_output_counter) {
            app_adapter.record_output_update();
        }
    }

    void apply_failure_side_effects(const encoder_app_adapter &app_adapter,
                                    const encoded_bridge_push_options &options) {
        if (options.rollback_pending_luma_on_failure) {
            (void) app_adapter.rollback_latest_pending_luma_state();
        }
    }

    void handle_fatal_stop(const encoder_app_adapter &app_adapter, const encoded_bridge_target target) {
        if (target == encoded_bridge_target::encoding_src) {
            app_adapter.request_fail_fast_stop("fatal appsrc push/EOS or caps-contract failure on encoding_src");
            return;
        }
        app_adapter.request_fail_fast_stop("fatal appsrc push/EOS or caps-contract failure on sei_src");
    }
} // namespace

encoded_bridge_step_result encoded_bridge_push_owned_buffer_via_orchestrator(
        encoded_bridge_context &context, const encoder_app_adapter &app_adapter, const encoded_bridge_target target,
        sf::unique_gst_buffer_ptr buffer, const encoded_bridge_push_options &options) {
    const auto result = encoded_bridge_push_owned_buffer(context, target, std::move(buffer));
    if (result.status == encoded_bridge_step_status::ok) {
        apply_success_side_effects(app_adapter, options);
        return result;
    }

    apply_failure_side_effects(app_adapter, options);
    if (result.status == encoded_bridge_step_status::fatal_stop) {
        handle_fatal_stop(app_adapter, target);
    }
    return result;
}

encoded_bridge_step_result encoded_bridge_push_buffer_ref_via_orchestrator(encoded_bridge_context &context,
                                                                           const encoder_app_adapter &app_adapter,
                                                                           const encoded_bridge_target target,
                                                                           GstBuffer *buffer,
                                                                           const encoded_bridge_push_options &options) {
    const auto result = encoded_bridge_push_buffer_ref(context, target, buffer);
    if (result.status == encoded_bridge_step_status::ok) {
        apply_success_side_effects(app_adapter, options);
        return result;
    }

    apply_failure_side_effects(app_adapter, options);
    if (result.status == encoded_bridge_step_status::fatal_stop) {
        handle_fatal_stop(app_adapter, target);
    }
    return result;
}

encoded_bridge_step_result encoded_bridge_signal_eos_via_orchestrator(encoded_bridge_context &context,
                                                                      const encoder_app_adapter &app_adapter,
                                                                      const encoded_bridge_target target) {
    const auto result = encoded_bridge_signal_eos(context, target);
    if (result.status == encoded_bridge_step_status::fatal_stop) {
        handle_fatal_stop(app_adapter, target);
    }
    return result;
}
