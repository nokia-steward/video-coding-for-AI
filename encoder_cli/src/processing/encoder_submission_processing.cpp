// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_submission_processing.h"

#include "../bridge/app/encoder_app_adapter.h"
#include "../bridge/context/encoder_bridge_context.h"
#include "../bridge/orchestration/encoded_bridge_orchestrator.h"
#include "../encoder_cli_debug.h"

#include <gst/gst.h>

#include "../../libvcai/include/logging.h"

GST_DEBUG_CATEGORY_STATIC(encoder_submission_processing);

void init_encoder_submission_processing_debug_category() {
    GST_DEBUG_CATEGORY_INIT(encoder_submission_processing, "encoder-submission-processing", 0,
                            "Async encoder appsrc submission for file mode");
}

namespace {

    [[nodiscard]] bool push_submission_item_to_appsrc(global_data &data, encoder_submission_item &item) {
        auto bridge_context = encoded_bridge_context{.encoding_src = data.encoded_ports.encoding_src,
                                                     .sei_src = data.encoded_ports.sei_src,
                                                     .runtime = &data.egress_bridge_runtime};
        const encoder_app_adapter app_adapter(data);
        const encoded_bridge_push_options options{
                .update_output_counter = false,
                .update_sei_injection_counter = false,
                .update_encoder_submission_counter = true,
                .rollback_pending_luma_on_failure = false,
        };

        const auto result = encoded_bridge_push_owned_buffer_via_orchestrator(
                bridge_context, app_adapter, encoded_bridge_target::encoding_src, std::move(item.buffer), options);
        return result.status == encoded_bridge_step_status::ok ||
               result.status == encoded_bridge_step_status::recoverable_drop;
    }

    enum class submission_iter_result { Continue, ExitLoop };

    [[nodiscard]] submission_iter_result process_encoder_submission_iteration(global_data &data) {
        auto item_opt = data.encoder_submission_queue.pop();
        if (!item_opt.has_value()) {
            sf::info(encoder_submission_processing, "Encoder submission queue drained.");
            return submission_iter_result::ExitLoop;
        }

        if (!push_submission_item_to_appsrc(data, *item_opt)) {
            (void) data.remove_latest_pending_encoded_contract_state();
            sf::error(encoder_submission_processing, "Failed to push frame to encoding appsrc");
        }

        return submission_iter_result::Continue;
    }

    struct encoder_submission_task_userdata {
        global_data *data = nullptr;
        GstTask *self_task = nullptr;
    };

    void encoder_submission_task_entry(gpointer user_data) {
        auto *ud = static_cast<encoder_submission_task_userdata *>(user_data);
        const auto step = process_encoder_submission_iteration(*ud->data);
        if (step == submission_iter_result::ExitLoop) {
            gst_task_stop(GST_TASK(ud->self_task));
        }
    }

    void destroy_encoder_submission_task_userdata(gpointer user_data) {
        delete static_cast<encoder_submission_task_userdata *>(user_data);
    }

} // namespace

bool start_encoder_submission_gst_task(global_data &data, bypass_gst_poll_slot &slot) {
    if (slot.task != nullptr) {
        return false;
    }

    // RAII_EXEMPT: GstTask user_data; freed in destroy_encoder_submission_task_userdata.
    auto *ud = new encoder_submission_task_userdata{};
    ud->data = &data;

    GstTask *const task = gst_task_new(encoder_submission_task_entry, ud, destroy_encoder_submission_task_userdata);
    if (!task) {
        delete ud;
        sf::error(encoder_submission_processing, "Failed to create GstTask for encoder submission");
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &slot.lock);
    if (!gst_task_start(task)) {
        sf::error(encoder_submission_processing, "Failed to start GstTask for encoder submission");
        gst_object_unref(task);
        return false;
    }

    slot.task = task;
    sf::info(encoder_submission_processing, "Encoder submission GstTask started");
    return true;
}

void join_encoder_submission_gst_task(global_data & /*data*/, bypass_gst_poll_slot &slot) {
    if (slot.task == nullptr) {
        return;
    }
    GstTask *const task = slot.task;
    slot.task = nullptr;

    if (!gst_task_join(task)) {
        sf::warning(encoder_submission_processing, "gst_task_join failed for encoder submission GstTask");
    }
    gst_object_unref(task);
}
