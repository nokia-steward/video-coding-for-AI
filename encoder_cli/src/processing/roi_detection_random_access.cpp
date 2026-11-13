// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file roi_detection_random_access.cpp
 * @brief Implements random access frame processing for random_access_packer mode.
 */

#include "roi_detection_random_access.h"

#include <atomic>
#include <chrono>
#include <gst/gst.h>
#include <memory>

#include "../../../libvcai/include/logging.h"
#include "../bridge/app/random_access_app_adapter.h"
#include "../bridge/context/random_access_bridge_context.h"
#include "../bridge/orchestration/random_access_bridge_orchestrator.h"
#include "../global_data.h"
#include "../roi/pri_packer_view.h"
#include "../roi/random_access_packer.h"

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace ra_gst_detail {

    enum class random_access_iter_result { Continue, ExitLoop };

    [[nodiscard]] random_access_iter_result
    random_access_frame_processing_iteration(global_data &data, random_access_bridge_context &context,
                                             const random_access_app_adapter &app_adapter,
                                             const GstClockTime frame_duration, GstClockTime &next_pts,
                                             std::chrono::steady_clock::time_point &last_push_time) {
        if (data.stop_processing.load()) {
            return random_access_iter_result::ExitLoop;
        }

        auto period_opt = context.packer->get_complete_period();
        if (!period_opt.has_value()) {
            sf::info(roi_detection_processing, "Frame Processing thread finished (no more periods available).");
            return random_access_iter_result::ExitLoop;
        }

        const auto period_result = random_access_process_period_via_orchestrator(
                context, app_adapter, *period_opt, frame_duration, next_pts, last_push_time);
        if (period_result.status == random_access_bridge_core::step_status::fatal_stop ||
            period_result.status == random_access_bridge_core::step_status::drain_complete) {
            return random_access_iter_result::ExitLoop;
        }

        return random_access_iter_result::Continue;
    }

    struct random_access_task_userdata {
        global_data *data = nullptr;
        random_access_bridge_context context{.data = nullptr, .packer = nullptr};
        std::unique_ptr<random_access_app_adapter> app_adapter{};
        GstClockTime frame_duration = 0;
        GstClockTime next_pts = GST_CLOCK_TIME_NONE;
        std::chrono::steady_clock::time_point last_push_time{};
        GstTask *self_task = nullptr;
        std::atomic<bool> epilogue_done{false};
    };

    void random_access_epilogue_once(random_access_task_userdata *ud) {
        bool expected = false;
        if (!ud->epilogue_done.compare_exchange_strong(expected, true)) {
            return;
        }
        (void) random_access_cleanup_via_orchestrator(ud->context, *ud->app_adapter);
    }

    void random_access_task_entry(gpointer user_data) {
        auto *ud = static_cast<random_access_task_userdata *>(user_data);
        const auto step = random_access_frame_processing_iteration(
                *ud->data, ud->context, *ud->app_adapter, ud->frame_duration, ud->next_pts, ud->last_push_time);
        if (step == random_access_iter_result::ExitLoop) {
            random_access_epilogue_once(ud);
            gst_task_stop(GST_TASK(ud->self_task));
        }
    }

    void destroy_random_access_task_userdata(gpointer user_data) {
        delete static_cast<random_access_task_userdata *>(user_data);
    }

} // namespace ra_gst_detail

void random_access_frame_processing(global_data &data) {
    sf::info(roi_detection_processing, "Frame Processing thread started (random_access mode)");

    auto context = random_access_bridge_context{.data = &data, .packer = nullptr};
    const random_access_app_adapter app_adapter(data);
    if (random_access_get_packer_via_orchestrator(context, app_adapter) == nullptr) {
        return;
    }

    const int framerate = data.params.framerate > 0 ? data.params.framerate : 30;
    const auto frame_duration = static_cast<GstClockTime>(GST_SECOND / framerate);
    GstClockTime next_pts = GST_CLOCK_TIME_NONE;
    auto last_push_time = std::chrono::steady_clock::now();

    using ra_gst_detail::random_access_frame_processing_iteration;
    using ra_gst_detail::random_access_iter_result;
    while (random_access_frame_processing_iteration(data, context, app_adapter, frame_duration, next_pts,
                                                    last_push_time) == random_access_iter_result::Continue) {
    }

    (void) random_access_cleanup_via_orchestrator(context, app_adapter);
}

bool start_random_access_gst_task(global_data &data, bypass_gst_poll_slot &slot) {
    if (slot.task != nullptr) {
        return false;
    }
    if (!data.params.pack_rois) {
        return true;
    }

    if (!pri_packer_view(data.packer).has_active_random_access()) {
        return true;
    }

    using ra_gst_detail::destroy_random_access_task_userdata;
    using ra_gst_detail::random_access_task_entry;
    using ra_gst_detail::random_access_task_userdata;

    // RAII_EXEMPT: GstTask user_data; freed in destroy_random_access_task_userdata.
    auto *ud = new random_access_task_userdata{};
    ud->data = &data;
    ud->context = random_access_bridge_context{.data = &data, .packer = nullptr};
    ud->app_adapter = std::make_unique<random_access_app_adapter>(data);
    if (random_access_get_packer_via_orchestrator(ud->context, *ud->app_adapter) == nullptr) {
        delete ud;
        return true;
    }

    const int framerate = data.params.framerate > 0 ? data.params.framerate : 30;
    ud->frame_duration = static_cast<GstClockTime>(GST_SECOND / framerate);
    ud->last_push_time = std::chrono::steady_clock::now();

    GstTask *const task = gst_task_new(random_access_task_entry, ud, destroy_random_access_task_userdata);
    if (!task) {
        delete ud;
        sf::error(roi_detection_processing, "Failed to create GstTask for random-access processing");
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &slot.lock);
    sf::info(roi_detection_processing, "Random-access GstTask starting");
    if (!gst_task_start(task)) {
        sf::error(roi_detection_processing, "Failed to start GstTask for random-access processing");
        gst_object_unref(task);
        return false;
    }

    slot.task = task;
    return true;
}

void join_random_access_gst_task(global_data & /*data*/, bypass_gst_poll_slot &slot) {
    if (slot.task == nullptr) {
        return;
    }
    GstTask *const task = slot.task;
    slot.task = nullptr;

    gst_task_stop(task);
    if (!gst_task_join(task)) {
        sf::warning(roi_detection_processing, "gst_task_join failed for random-access GstTask");
    }
    gst_object_unref(task);
}
