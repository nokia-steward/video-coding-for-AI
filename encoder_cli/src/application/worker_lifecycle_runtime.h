// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <cstdint>

/**
 * @file worker_lifecycle_runtime.h
 * @brief Per-instance lifecycle state for encoder worker orchestration.
 */

/**
 * @brief Lifecycle phases for one worker-cycle generation.
 */
enum class worker_lifecycle_phase : uint8_t { idle = 0, running = 1, stopping = 2, drained = 3 };

/**
 * @brief Tracks worker lifecycle transitions for one encoder instance.
 */
struct worker_lifecycle_runtime {
    std::atomic_uint64_t generation{0};
    std::atomic<worker_lifecycle_phase> phase{worker_lifecycle_phase::idle};
    std::atomic_bool stop_requested{false};
    std::atomic_bool processing_workers_active{false};
    std::atomic_bool message_workers_active{false};
    std::atomic_bool polling_workers_active{false};

    /**
     * @brief Starts a new worker-cycle generation.
     */
    void begin_cycle() {
        ++generation;
        phase = worker_lifecycle_phase::running;
        stop_requested = false;
        processing_workers_active = false;
        message_workers_active = false;
        polling_workers_active = false;
    }

    /**
     * @brief Requests stop transition once per generation.
     * @return True on first stop request, false when already requested.
     */
    [[nodiscard]] bool request_stop_once() {
        phase = worker_lifecycle_phase::stopping;
        return !stop_requested.exchange(true);
    }

    /**
     * @brief Marks lifecycle as drained after deterministic joins.
     */
    void mark_drained() {
        processing_workers_active = false;
        message_workers_active = false;
        phase = worker_lifecycle_phase::drained;
    }

    /**
     * @brief Resets lifecycle to idle for restart.
     */
    void reset() {
        phase = worker_lifecycle_phase::idle;
        stop_requested = false;
        processing_workers_active = false;
        message_workers_active = false;
        polling_workers_active = false;
    }
};
