// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ingress_bridge_orchestrator.h"

#include "../../encoded_frame_item.h"

#include <optional>

namespace {
    [[nodiscard]] std::optional<pending_encoded_contract_state>
    resolve_pending_encoded_contract_state(const ingress_app_adapter &app_adapter, GstDebugCategory *category) {
        const auto pending_state = app_adapter.pop_pending_contract_state();
        if (!pending_state.has_value() || pending_state->sequence_id == 0) {
            sf::error(category, "Missing or invalid pending encoded contract state for encoded sample");
            return std::nullopt;
        }
        return pending_state;
    }

    void maybe_close_file_raw_queue(ingress_bridge_context &context, const ingress_app_adapter &app_adapter,
                                    const guint64 pushed_frames, GstDebugCategory *category) {
        if (!app_adapter.should_force_close_file_raw_queue(pushed_frames)) {
            return;
        }
        if (!app_adapter.mark_file_input_forced_raw_eos_once()) {
            return;
        }

        sf::info(category, "File input reached expected frame count (%llu). Closing raw frame queue.",
                 static_cast<unsigned long long>(pushed_frames));
        app_adapter.close_raw_queue_once(context);
    }
} // namespace

GstFlowReturn ingress_handle_raw_new_sample_via_orchestrator(ingress_bridge_context &context,
                                                             const ingress_app_adapter &app_adapter, GstElement *sink,
                                                             GstDebugCategory *category) {
    auto pull_result = ingress_bridge_pull_sample_from_signal(sink);
    if (pull_result.status == ingress_bridge_step_status::recoverable_drop) {
        sf::warning(category, "Failed to pull sample from input sink");
        return GST_FLOW_ERROR;
    }
    if (pull_result.status == ingress_bridge_step_status::fatal_stop) {
        app_adapter.request_fail_fast_stop("raw callback pull failed due to invalid sink");
        return GST_FLOW_ERROR;
    }

    if (!app_adapter.should_process_raw_sample()) {
        const uint64_t dropped_count = app_adapter.record_dropped_raw_sample(context);
        sf::debug(category, "Dropping frame (not ready to process yet, dropped: %llu)",
                  static_cast<unsigned long long>(dropped_count));
        return GST_FLOW_OK;
    }

    if (!app_adapter.enqueue_raw_sample_with_contract(std::move(pull_result.sample), frame_path_type::inference,
                                                      category)) {
        return GST_FLOW_ERROR;
    }
    const guint64 pushed_frames = app_adapter.increment_input_frame_count();
    maybe_close_file_raw_queue(context, app_adapter, pushed_frames, category);
    return GST_FLOW_OK;
}

void ingress_handle_raw_eos_via_orchestrator(ingress_bridge_context &context, const ingress_app_adapter &app_adapter,
                                             GstDebugCategory *category) {
    sf::info(category, "EOS reached on input sink. Closing raw frame queue.");
    app_adapter.close_raw_queue_once(context);
}

GstFlowReturn ingress_handle_encoded_new_sample_via_orchestrator(ingress_bridge_context &context,
                                                                 const ingress_app_adapter &app_adapter,
                                                                 GstElement *sink, GstDebugCategory *category) {
    auto pull_result = ingress_bridge_pull_sample_from_signal(sink);
    if (pull_result.status == ingress_bridge_step_status::recoverable_drop) {
        return GST_FLOW_ERROR;
    }
    if (pull_result.status == ingress_bridge_step_status::fatal_stop) {
        app_adapter.request_fail_fast_stop("encoded callback pull failed due to invalid sink");
        return GST_FLOW_ERROR;
    }

    auto sample = std::move(pull_result.sample);
    if (auto *buffer = gst_sample_get_buffer(sample.get())) {
        const GstClockTime pts = GST_BUFFER_PTS(buffer);
        const GstClockTime dts = GST_BUFFER_DTS(buffer);
        const GstClockTime duration = GST_BUFFER_DURATION(buffer);
        const uint64_t frame_count = app_adapter.record_encoded_sample_log(context);
        if (frame_count <= 20U || pts >= GST_CLOCK_TIME_NONE / 2) {
            sf::info(category,
                     "Encoder OUTPUT frame #%llu: PTS=%" GST_TIME_FORMAT ", DTS=%" GST_TIME_FORMAT
                     ", duration=%" GST_TIME_FORMAT,
                     static_cast<unsigned long long>(frame_count), GST_TIME_ARGS(pts), GST_TIME_ARGS(dts),
                     GST_TIME_ARGS(duration));
        }
    }

    const auto pending_contract_state = resolve_pending_encoded_contract_state(app_adapter, category);
    if (!pending_contract_state.has_value()) {
        app_adapter.request_fail_fast_stop("encoded callback missing pending contract state");
        return GST_FLOW_ERROR;
    }
    app_adapter.enqueue_encoded_item(make_encoded_frame_item(std::move(sample), pending_contract_state->luma_adaptation,
                                                             pending_contract_state->sequence_id));
    return GST_FLOW_OK;
}

void ingress_handle_encoded_eos_via_orchestrator(ingress_bridge_context &context,
                                                 const ingress_app_adapter &app_adapter, GstDebugCategory *category) {
    sf::info(category, "EOS reached on encoding sink. Closing encoded frame queue.");
    app_adapter.close_encoded_queue_once(context);
}

ingress_bridge_pull_result ingress_try_pull_sample_via_orchestrator(ingress_bridge_context &context,
                                                                    const ingress_app_adapter &app_adapter,
                                                                    GstElement *appsink, const GstClockTime timeout) {
    (void) context;
    auto pull_result = ingress_bridge_try_pull_sample(appsink, timeout);
    if (pull_result.status == ingress_bridge_step_status::fatal_stop) {
        app_adapter.request_fail_fast_stop("polling path received invalid appsink");
    }
    return pull_result;
}

bool ingress_enqueue_merged_sample_via_orchestrator(ingress_bridge_context &context,
                                                    const ingress_app_adapter &app_adapter,
                                                    sf::unique_gst_sample_ptr sample, const frame_path_type path,
                                                    GstDebugCategory *category) {
    if (!app_adapter.should_process_raw_sample()) {
        const uint64_t dropped_count = app_adapter.record_dropped_raw_sample(context);
        sf::debug(category, "Dropping merged frame (not ready to process yet, dropped: %llu)",
                  static_cast<unsigned long long>(dropped_count));
        return false;
    }

    if (!app_adapter.enqueue_raw_sample_with_contract(std::move(sample), path, category)) {
        return false;
    }
    const guint64 pushed_frames = app_adapter.increment_input_frame_count();
    maybe_close_file_raw_queue(context, app_adapter, pushed_frames, category);
    return true;
}
