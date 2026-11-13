// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "appsink_polling.h"

#include "../../../../libvcai/include/logging.h"
#include "../../bridge/app/ingress_app_adapter.h"
#include "../../bridge/context/ingress_bridge_context.h"
#include "../../bridge/orchestration/ingress_bridge_orchestrator.h"
#include "../../encoder_enums.h"
#include "../../global_data.h"
#include "frame_merger.h"
#include "input_ingress_facade.h"

#include <atomic>
#include <gst/app/gstappsink.h>
#include <optional>

/**
 * @file appsink_polling.cpp
 * @brief Appsink polling for dual-path bypass: GstTask workers plus synchronous test helpers.
 */

/// GStreamer debug category for appsink polling.
GST_DEBUG_CATEGORY_STATIC(appsink_polling);

void init_appsink_polling_debug_category() {
    GST_DEBUG_CATEGORY_INIT(appsink_polling, "appsink-polling", 0, "Appsink polling threads for dual-path bypass");
}

namespace {

    /**
     * @brief Per-loop state for ingress bridge pulls (shared by blocking loop and GstTask iterations).
     */
    struct appsink_poll_loop_state {
        appsink_poll_context ctx;
        ingress_bridge_context bridge_context{};
        std::optional<ingress_app_adapter> app_adapter;

        explicit appsink_poll_loop_state(appsink_poll_context c) :
            ctx(std::move(c)), bridge_context{}, app_adapter(std::nullopt) {
            if (ctx.data != nullptr) {
                // Rebuild the narrow view transiently: only the stable references to the aggregate
                // members (ingress runtime + app host) are retained, not the view itself.
                const input_ingress_facade view(*ctx.data);
                bridge_context.runtime = &view.ingress_runtime;
                app_adapter.emplace(view.instance());
            }
        }
    };

    void finalize_appsink_poll_path(const appsink_poll_context &ctx, const char *path_name) {
        const int eos_count = ctx.eos_counter->fetch_add(1) + 1;
        sf::info(appsink_polling, "%s polling worker exiting (EOS count: %d)", path_name, eos_count);

        if (eos_count >= 2) {
            sf::info(appsink_polling, "Both appsinks done, closing frame merger");
            ctx.merger->close();
        }
    }

    /**
     * @return true to continue polling, false when the path should end (EOS, fatal, or stop before pull).
     */
    [[nodiscard]] bool appsink_poll_single_step(appsink_poll_loop_state &state, const char *path_name) {
        auto &ctx = state.ctx;
        if (ctx.stop_flag->load()) {
            return false;
        }

        ingress_bridge_pull_result pull_result{};
        if (state.app_adapter.has_value()) {
            pull_result = ingress_try_pull_sample_via_orchestrator(state.bridge_context, *state.app_adapter,
                                                                   ctx.appsink, 100 * GST_MSECOND);
        } else {
            pull_result = ingress_bridge_try_pull_sample(ctx.appsink, 100 * GST_MSECOND);
        }

        if (pull_result.status == ingress_bridge_step_status::recoverable_drop) {
            return true;
        }
        if (pull_result.status == ingress_bridge_step_status::drain_complete) {
            sf::info(appsink_polling, "%s appsink received EOS", path_name);
            return false;
        }
        if (pull_result.status == ingress_bridge_step_status::fatal_stop) {
            sf::warning(appsink_polling, "%s polling worker stopping after fatal pull failure", path_name);
            return false;
        }

        if (ctx.is_inference_path) {
            ctx.merger->add_inference_frame(std::move(pull_result.sample));
        } else {
            ctx.merger->add_bypass_frame(std::move(pull_result.sample));
        }
        return true;
    }

    void run_appsink_poll_until_done(appsink_poll_context ctx, const char *path_name) {
        sf::info(appsink_polling, "%s polling started (blocking)", path_name);
        appsink_poll_loop_state state(std::move(ctx));
        while (true) {
            if (state.ctx.stop_flag->load()) {
                break;
            }
            if (!appsink_poll_single_step(state, path_name)) {
                break;
            }
        }
        finalize_appsink_poll_path(state.ctx, path_name);
    }

    struct appsink_poll_task_userdata {
        appsink_poll_loop_state loop;
        GstTask *self_task = nullptr;
        std::atomic<bool> finalized{false};
        const char *path_name = nullptr;

        appsink_poll_task_userdata(appsink_poll_context ctx, const char *name) :
            loop(std::move(ctx)), path_name(name) {}
    };

    void finalize_appsink_poll_once(appsink_poll_task_userdata *ud) {
        bool expected = false;
        if (!ud->finalized.compare_exchange_strong(expected, true)) {
            return;
        }
        finalize_appsink_poll_path(ud->loop.ctx, ud->path_name);
    }

    void appsink_poll_task_entry(gpointer user_data) {
        auto *ud = static_cast<appsink_poll_task_userdata *>(user_data);
        if (ud->loop.ctx.stop_flag->load()) {
            finalize_appsink_poll_once(ud);
            gst_task_stop(GST_TASK(ud->self_task));
            return;
        }
        if (!appsink_poll_single_step(ud->loop, ud->path_name)) {
            finalize_appsink_poll_once(ud);
            gst_task_stop(GST_TASK(ud->self_task));
        }
    }

    void destroy_appsink_poll_userdata(gpointer user_data) {
        delete static_cast<appsink_poll_task_userdata *>(user_data);
    }

    struct merger_poll_task_userdata {
        // Stable raw aggregate handle (escape hatch): live GStream task holds it, so it must not
        // point at a stack-local facade. The narrow view is rebuilt transiently per invocation.
        global_data *data = nullptr;
        GstTask *self_task = nullptr;
        std::atomic<bool> finalized{false};
    };

    void merger_consumer_close_raw_queue_once(merger_poll_task_userdata *ud) {
        bool expected = false;
        if (!ud->finalized.compare_exchange_strong(expected, true)) {
            return;
        }
        sf::info(appsink_polling, "Merger consumer exiting, closing raw_frame_queue");
        const input_ingress_facade view(*ud->data);
        auto bridge_context = ingress_bridge_context{.runtime = &view.ingress_runtime};
        const ingress_app_adapter app_adapter(view.instance());
        app_adapter.close_raw_queue_once(bridge_context);
    }

    void merger_poll_task_entry(gpointer user_data) {
        auto *ud = static_cast<merger_poll_task_userdata *>(user_data);
        input_ingress_facade data(*ud->data);
        if (!data.bypass.merger) {
            sf::warning(appsink_polling, "Frame merger is not initialized, stopping merger GstTask");
            merger_consumer_close_raw_queue_once(ud);
            gst_task_stop(GST_TASK(ud->self_task));
            return;
        }

        auto frame_opt = data.bypass.merger->get_next_frame();
        if (!frame_opt) {
            merger_consumer_close_raw_queue_once(ud);
            gst_task_stop(GST_TASK(ud->self_task));
            return;
        }

        auto &frame = *frame_opt;
        auto bridge_context = ingress_bridge_context{.runtime = &data.ingress_runtime};
        const ingress_app_adapter app_adapter(data.instance());
        (void) ingress_enqueue_merged_sample_via_orchestrator(bridge_context, app_adapter, std::move(frame.sample),
                                                              frame.path, appsink_polling);
    }

    void destroy_merger_poll_userdata(gpointer user_data) {
        delete static_cast<merger_poll_task_userdata *>(user_data);
    }

} // namespace

void inference_appsink_poll_thread(appsink_poll_context ctx) {
    ctx.is_inference_path = true;
    run_appsink_poll_until_done(std::move(ctx), "Inference");
}

void bypass_appsink_poll_thread(appsink_poll_context ctx) {
    ctx.is_inference_path = false;
    run_appsink_poll_until_done(std::move(ctx), "Bypass");
}

void merger_consumer_thread(input_ingress_facade &data) {
    sf::info(appsink_polling, "Merger consumer thread started (blocking)");

    while (!data.stop_processing.load()) {
        if (!data.bypass.merger) {
            sf::warning(appsink_polling, "Frame merger is not initialized, stopping consumer thread");
            break;
        }

        auto frame_opt = data.bypass.merger->get_next_frame();

        if (!frame_opt) {
            break;
        }

        auto &frame = *frame_opt;

        auto bridge_context = ingress_bridge_context{.runtime = &data.ingress_runtime};
        const ingress_app_adapter app_adapter(data.instance());
        (void) ingress_enqueue_merged_sample_via_orchestrator(bridge_context, app_adapter, std::move(frame.sample),
                                                              frame.path, appsink_polling);
    }

    sf::info(appsink_polling, "Merger consumer thread exiting, closing raw_frame_queue");
    auto bridge_context = ingress_bridge_context{.runtime = &data.ingress_runtime};
    const ingress_app_adapter app_adapter(data.instance());
    app_adapter.close_raw_queue_once(bridge_context);
}

bool start_appsink_poll_gst_task(bypass_gst_poll_slot &slot, appsink_poll_context ctx, const char *path_name) {
    if (slot.task != nullptr) {
        sf::warning(appsink_polling, "%s GstTask already running", path_name);
        return false;
    }

    // RAII_EXEMPT: Heap user_data for GstTask; freed by destroy_appsink_poll_userdata when the task is destroyed.
    auto *ud = new appsink_poll_task_userdata(std::move(ctx), path_name);
    GstTask *const task = gst_task_new(appsink_poll_task_entry, ud, destroy_appsink_poll_userdata);
    if (!task) {
        delete ud;
        sf::error(appsink_polling, "Failed to create GstTask for %s appsink polling", path_name);
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &slot.lock);
    sf::info(appsink_polling, "%s GstTask polling worker starting", path_name);
    if (!gst_task_start(task)) {
        sf::error(appsink_polling, "Failed to start GstTask for %s appsink polling", path_name);
        gst_object_unref(task);
        return false;
    }

    slot.task = task;
    return true;
}

void join_appsink_poll_gst_task(bypass_gst_poll_slot &slot) {
    if (slot.task == nullptr) {
        return;
    }
    GstTask *const task = slot.task;
    slot.task = nullptr;

    gst_task_stop(task);
    if (!gst_task_join(task)) {
        sf::warning(appsink_polling, "gst_task_join failed for bypass appsink poll task");
    }
    gst_object_unref(task);
}

bool start_merger_poll_gst_task(input_ingress_facade &data, bypass_gst_poll_slot &slot) {
    if (slot.task != nullptr) {
        sf::warning(appsink_polling, "Merger GstTask already running");
        return false;
    }

    // RAII_EXEMPT: Heap user_data for GstTask; freed by destroy_merger_poll_userdata when the task is destroyed.
    auto *ud = new merger_poll_task_userdata{};
    // Store the stable aggregate handle (escape hatch), not the transient view.
    ud->data = &data.instance();
    GstTask *const task = gst_task_new(merger_poll_task_entry, ud, destroy_merger_poll_userdata);
    if (!task) {
        delete ud;
        sf::error(appsink_polling, "Failed to create GstTask for merger consumer");
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &slot.lock);
    sf::info(appsink_polling, "Merger GstTask consumer starting");
    if (!gst_task_start(task)) {
        sf::error(appsink_polling, "Failed to start GstTask for merger consumer");
        gst_object_unref(task);
        return false;
    }

    slot.task = task;
    return true;
}

void join_merger_poll_gst_task(input_ingress_facade &data, bypass_gst_poll_slot &slot) {
    if (slot.task == nullptr) {
        return;
    }
    if (data.bypass.merger) {
        data.bypass.merger->request_consumer_shutdown();
    }
    GstTask *const task = slot.task;
    slot.task = nullptr;

    gst_task_stop(task);
    if (!gst_task_join(task)) {
        sf::warning(appsink_polling, "gst_task_join failed for merger poll task");
    }
    gst_object_unref(task);
}
