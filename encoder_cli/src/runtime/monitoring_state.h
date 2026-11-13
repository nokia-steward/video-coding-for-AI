// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../libvcai/include/pipeline_fps_monitor.h"
#include "../reporting/performance_monitor.h"
#include "../reporting/simple_status_reporter.h"

#include <memory>

/**
 * @file monitoring_state.h
 * @brief Groups runtime monitoring/reporting components.
 */

/**
 * @brief Groups runtime monitoring/reporting components.
 */
struct monitoring_state {
    monitoring_state() : status_reporter(nullptr), perf_monitor(nullptr), fps_monitor() {}

    std::unique_ptr<simple_status_reporter> status_reporter;
    std::unique_ptr<performance_monitor> perf_monitor;
    sf::pipeline_fps_monitor fps_monitor;
};
