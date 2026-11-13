// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "app_periodic_gst_tasks.h"

#include "../encoder_cli_debug.h"
#include "../global_data.h"
#include "../reporting/performance_monitor.h"
#include "../reporting/simple_status_reporter.h"

#include <chrono>
#include <glib.h>
#include <gst/gst.h>

#include "../../libvcai/include/logging.h"

/**
 * @file app_periodic_gst_tasks.cpp
 * @brief GstTask implementations for file-mode watchdog and unified reporting ticks.
 */

namespace {

    constexpr guint watchdog_slice_us = 100 * 1000;
    constexpr int watchdog_slices_per_tick = 10;
    const auto file_mode_watchdog_idle_timeout = std::chrono::seconds(8);

    constexpr guint reporting_tick_us = 100 * 1000;

    void join_gst_task_slot(bypass_gst_poll_slot &slot) {
        if (slot.task == nullptr) {
            return;
        }
        GstTask *const task = slot.task;
        slot.task = nullptr;
        gst_task_stop(task);
        if (!gst_task_join(task)) {
            sf::warning(encoder_cli_main, "gst_task_join failed for periodic GstTask");
        }
        gst_object_unref(task);
    }

    struct file_watchdog_userdata {
        global_data *data = nullptr;
        GstTask *self_task = nullptr;
        guint64 expected_frames = 0;
        std::chrono::steady_clock::time_point last_progress_at{};
        int last_frame_number = 0;
    };

    void file_watchdog_task_entry(gpointer user_data) {
        auto *ud = static_cast<file_watchdog_userdata *>(user_data);
        global_data &data = *ud->data;

        for (int i = 0; i < watchdog_slices_per_tick; ++i) {
            if (data.stop_processing.load()) {
                gst_task_stop(GST_TASK(ud->self_task));
                return;
            }
            g_usleep(watchdog_slice_us);
        }

        if (data.stop_processing.load()) {
            gst_task_stop(GST_TASK(ud->self_task));
            return;
        }

        const int current_frame_number = data.current_frame_number.load();
        if (current_frame_number != ud->last_frame_number) {
            ud->last_frame_number = current_frame_number;
            ud->last_progress_at = std::chrono::steady_clock::now();
        }

        if (current_frame_number >= static_cast<int>(ud->expected_frames)) {
            const auto idle_for = std::chrono::steady_clock::now() - ud->last_progress_at;
            if (idle_for >= file_mode_watchdog_idle_timeout && data.request_worker_stop_once(true)) {
                data.file_mode_watchdog_triggered.store(true);
                sf::warning(encoder_cli_main,
                            "File-mode shutdown watchdog forcing cleanup after %llds idle with frame_number=%d "
                            "expected=%llu",
                            static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(idle_for).count()),
                            current_frame_number, static_cast<unsigned long long>(ud->expected_frames));
                if (data.monitoring.status_reporter) {
                    data.monitoring.status_reporter->report_event("Application shutdown initiated");
                }
                if (!data.shutdown_started.load()) {
                    sf::warning(encoder_cli_main,
                                "File-mode shutdown watchdog invoking global_data::shutdown for forced pipeline "
                                "teardown");
                    data.shutdown();
                }
                gst_task_stop(GST_TASK(ud->self_task));
            }
        }
    }

    void destroy_file_watchdog_userdata(gpointer user_data) { delete static_cast<file_watchdog_userdata *>(user_data); }

    struct reporting_task_userdata {
        global_data *data = nullptr;
        GstTask *self_task = nullptr;
        std::chrono::steady_clock::time_point last_status_report{};
        std::chrono::steady_clock::time_point last_perf_report{};
    };

    void reporting_task_entry(gpointer user_data) {
        auto *ud = static_cast<reporting_task_userdata *>(user_data);
        global_data &data = *ud->data;

        g_usleep(reporting_tick_us);

        if (data.stop_processing.load()) {
            gst_task_stop(GST_TASK(ud->self_task));
            return;
        }

        const bool want_status = data.monitoring.status_reporter != nullptr &&
                                 data.monitoring.status_reporter->is_periodic_reporting_active();
        const bool want_perf =
                data.monitoring.perf_monitor != nullptr && data.monitoring.perf_monitor->is_periodic_reporting_active();

        if (!want_status && !want_perf) {
            gst_task_stop(GST_TASK(ud->self_task));
            return;
        }

        const auto now = std::chrono::steady_clock::now();

        if (want_status) {
            const auto interval =
                    std::chrono::seconds(data.monitoring.status_reporter->get_reporting_interval_seconds());
            if (now - ud->last_status_report >= interval) {
                data.monitoring.status_reporter->generate_status_report();
                ud->last_status_report = now;
            }
        }

        if (want_perf) {
            const auto interval = std::chrono::seconds(data.monitoring.perf_monitor->get_reporting_interval_seconds());
            if (now - ud->last_perf_report >= interval) {
                data.monitoring.perf_monitor->generate_periodic_report();
                data.monitoring.perf_monitor->update_periodic_report_interval_baseline();
                ud->last_perf_report = now;
            }
        }
    }

    void destroy_reporting_task_userdata(gpointer user_data) {
        delete static_cast<reporting_task_userdata *>(user_data);
    }

} // namespace

void start_file_mode_watchdog_gst_task(global_data &data) {
    if (data.file_mode_watchdog_slot.task != nullptr) {
        return;
    }
    if (data.params.input_type_value != input_type::file || !data.expected_file_input_frames.has_value()) {
        return;
    }

    // RAII_EXEMPT: GstTask user_data; freed in destroy_file_watchdog_userdata.
    auto *ud = new file_watchdog_userdata{};
    ud->data = &data;
    ud->expected_frames = *data.expected_file_input_frames;
    ud->last_frame_number = data.current_frame_number.load();
    ud->last_progress_at = std::chrono::steady_clock::now();

    GstTask *const task = gst_task_new(file_watchdog_task_entry, ud, destroy_file_watchdog_userdata);
    if (!task) {
        delete ud;
        sf::error(encoder_cli_main, "Failed to create GstTask for file-mode watchdog");
        return;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &data.file_mode_watchdog_slot.lock);
    if (!gst_task_start(task)) {
        sf::error(encoder_cli_main, "Failed to start GstTask for file-mode watchdog");
        gst_object_unref(task);
        return;
    }
    data.file_mode_watchdog_slot.task = task;
    sf::info(encoder_cli_main, "File-mode shutdown watchdog GstTask started");
}

void join_file_mode_watchdog_gst_task(global_data &data) { join_gst_task_slot(data.file_mode_watchdog_slot); }

bool start_app_periodic_reporting_gst_task(global_data &data) {
    if (data.app_periodic_reporting_slot.task != nullptr) {
        return true;
    }

    const bool want_status = data.monitoring.status_reporter != nullptr &&
                             data.monitoring.status_reporter->is_periodic_reporting_active();
    const bool want_perf =
            data.monitoring.perf_monitor != nullptr && data.monitoring.perf_monitor->is_periodic_reporting_active();

    if (!want_status && !want_perf) {
        return true;
    }

    // RAII_EXEMPT: GstTask user_data; freed in destroy_reporting_task_userdata.
    auto *ud = new reporting_task_userdata{};
    ud->data = &data;
    const auto t0 = std::chrono::steady_clock::now();
    ud->last_status_report = t0;
    ud->last_perf_report = t0;

    GstTask *const task = gst_task_new(reporting_task_entry, ud, destroy_reporting_task_userdata);
    if (!task) {
        delete ud;
        sf::error(encoder_cli_main, "Failed to create GstTask for periodic reporting");
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &data.app_periodic_reporting_slot.lock);
    if (!gst_task_start(task)) {
        sf::error(encoder_cli_main, "Failed to start GstTask for periodic reporting");
        gst_object_unref(task);
        return false;
    }
    data.app_periodic_reporting_slot.task = task;
    sf::info(encoder_cli_main, "Periodic reporting GstTask started");
    return true;
}

void join_app_periodic_reporting_gst_task(global_data &data) { join_gst_task_slot(data.app_periodic_reporting_slot); }
