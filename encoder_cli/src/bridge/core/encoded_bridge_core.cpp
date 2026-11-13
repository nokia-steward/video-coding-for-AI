// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoded_bridge_core.h"

#include <gst/app/gstappsrc.h>
#include <string>

#include "../../../libvcai/include/logging.h"
#include "../../encoder_cli_debug.h"

namespace {
    [[nodiscard]] GstElement *resolve_target_appsrc(const encoded_bridge_context &context,
                                                    const encoded_bridge_target target) {
        if (target == encoded_bridge_target::encoding_src) {
            return context.encoding_src;
        }
        return context.sei_src;
    }

    [[nodiscard]] std::atomic_bool *resolve_target_eos_flag(encoded_bridge_context &context,
                                                            const encoded_bridge_target target) {
        if (!context.runtime) {
            return nullptr;
        }
        if (target == encoded_bridge_target::encoding_src) {
            return &context.runtime->encoding_eos_sent;
        }
        return &context.runtime->sei_eos_sent;
    }

    [[nodiscard]] const char *target_name(const encoded_bridge_target target) {
        if (target == encoded_bridge_target::encoding_src) {
            return "encoding_src";
        }
        return "sei_src";
    }

    [[nodiscard]] bool is_encoding_target(const encoded_bridge_target target) {
        return target == encoded_bridge_target::encoding_src;
    }

    [[nodiscard]] sf::unique_gst_caps_ptr read_appsrc_caps(GstElement *const appsrc) {
        GstCaps *caps = nullptr;
        g_object_get(G_OBJECT(appsrc), "caps", &caps, nullptr);
        return sf::unique_gst_caps_ptr(caps);
    }

    [[nodiscard]] bool validate_push_caps_contract(encoded_bridge_context &context, const encoded_bridge_target target,
                                                   GstElement *const appsrc) {
        if (context.runtime == nullptr) {
            sf::error(encoder_cli_main, "Cannot validate %s caps contract: runtime is null", target_name(target));
            return false;
        }

        const bool encoding_target = is_encoding_target(target);
        if (!context.runtime->is_startup_configured(encoding_target)) {
            sf::error(encoder_cli_main, "Caps contract for %s was not configured before push", target_name(target));
            return false;
        }

        const sf::unique_gst_caps_ptr current_caps = read_appsrc_caps(appsrc);
        if (!current_caps) {
            sf::error(encoder_cli_main, "Caps contract mismatch on %s: runtime appsrc has no caps",
                      target_name(target));
            return false;
        }

        std::string expected_caps_text;
        std::string current_caps_text;
        if (!context.runtime->validate_caps_contract(encoding_target, current_caps.get(), &expected_caps_text,
                                                     &current_caps_text)) {
            sf::error(encoder_cli_main,
                      "Caps contract mismatch on %s (fatal): expected_locked_caps=\"%s\" current_caps=\"%s\"",
                      target_name(target), expected_caps_text.c_str(), current_caps_text.c_str());
            return false;
        }

        sf::debug(encoder_cli_main, "Caps contract verified for %s: %s", target_name(target),
                  current_caps_text.c_str());
        return true;
    }

    [[nodiscard]] encoded_bridge_step_result make_result(const encoded_bridge_step_status status,
                                                         const GstFlowReturn flow_return) {
        return {.status = status, .flow_return = flow_return};
    }
} // namespace

encoded_bridge_step_result encoded_bridge_push_owned_buffer(encoded_bridge_context &context,
                                                            const encoded_bridge_target target,
                                                            sf::unique_gst_buffer_ptr buffer) {
    if (!buffer) {
        return make_result(encoded_bridge_step_status::recoverable_drop, GST_FLOW_ERROR);
    }

    auto *const appsrc = resolve_target_appsrc(context, target);
    if (!appsrc || !GST_IS_APP_SRC(appsrc)) {
        return make_result(encoded_bridge_step_status::fatal_stop, GST_FLOW_ERROR);
    }
    if (!validate_push_caps_contract(context, target, appsrc)) {
        return make_result(encoded_bridge_step_status::fatal_stop, GST_FLOW_ERROR);
    }

    const GstFlowReturn flow_return = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer.release());
    if (flow_return == GST_FLOW_OK) {
        return make_result(encoded_bridge_step_status::ok, flow_return);
    }
    if (flow_return == GST_FLOW_FLUSHING) {
        return make_result(encoded_bridge_step_status::recoverable_drop, flow_return);
    }
    return make_result(encoded_bridge_step_status::fatal_stop, flow_return);
}

encoded_bridge_step_result encoded_bridge_push_buffer_ref(encoded_bridge_context &context,
                                                          const encoded_bridge_target target, GstBuffer *buffer) {
    if (!buffer) {
        return make_result(encoded_bridge_step_status::recoverable_drop, GST_FLOW_ERROR);
    }
    return encoded_bridge_push_owned_buffer(context, target, sf::unique_gst_buffer_ptr(gst_buffer_ref(buffer)));
}

encoded_bridge_step_result encoded_bridge_signal_eos(encoded_bridge_context &context,
                                                     const encoded_bridge_target target) {
    auto *const eos_sent_flag = resolve_target_eos_flag(context, target);
    if (eos_sent_flag == nullptr) {
        return make_result(encoded_bridge_step_status::fatal_stop, GST_FLOW_ERROR);
    }
    if (eos_sent_flag->load()) {
        return make_result(encoded_bridge_step_status::ok, GST_FLOW_OK);
    }

    auto *const appsrc = resolve_target_appsrc(context, target);
    if (!appsrc || !GST_IS_APP_SRC(appsrc)) {
        return make_result(encoded_bridge_step_status::fatal_stop, GST_FLOW_ERROR);
    }

    const GstFlowReturn flow_return = gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
    if (flow_return == GST_FLOW_OK || flow_return == GST_FLOW_FLUSHING) {
        eos_sent_flag->store(true);
        if (flow_return == GST_FLOW_OK) {
            return make_result(encoded_bridge_step_status::ok, flow_return);
        }
        return make_result(encoded_bridge_step_status::recoverable_drop, flow_return);
    }

    return make_result(encoded_bridge_step_status::fatal_stop, flow_return);
}
