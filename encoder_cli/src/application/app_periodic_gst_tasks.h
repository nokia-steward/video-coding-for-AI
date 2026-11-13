// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

struct global_data;

/**
 * @file app_periodic_gst_tasks.h
 * @brief GstTask drivers for file-mode idle watchdog and periodic status/perf reporting.
 */

void start_file_mode_watchdog_gst_task(global_data &data);
void join_file_mode_watchdog_gst_task(global_data &data);

[[nodiscard]] bool start_app_periodic_reporting_gst_task(global_data &data);
void join_app_periodic_reporting_gst_task(global_data &data);
