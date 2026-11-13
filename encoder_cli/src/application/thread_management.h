// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <memory>
#include <optional>
#include <thread>
#include "../global_data.h"

/**
 * @file thread_management.h
 * @brief Declares functions for managing processing threads.
 */

/**
 * @brief Waits for ROI, random-access, and encoded GstTasks in deterministic join order.
 */
void wait_for_threads_completion(bypass_gst_poll_slot &roi_detection_slot, bypass_gst_poll_slot &random_access_slot,
                                 bypass_gst_poll_slot &encoded_frame_slot, global_data &data);

/**
 * @brief Worker start options for mode-level orchestration.
 */
struct worker_thread_start_options {
    bool start_input_message_thread = false;
    bool start_output_message_thread = false;
};

/**
 * @brief Aggregates processing and message worker handles for one mode execution cycle.
 */
struct worker_thread_bundle {
    bypass_gst_poll_slot roi_detection_slot{};
    bypass_gst_poll_slot random_access_slot{};
    bypass_gst_poll_slot encoded_frame_slot{};
    bypass_gst_poll_slot encoder_submission_slot{};
    /// Pipeline bus polling via GstTask (same slot type as bypass workers).
    bypass_gst_poll_slot input_message_slot{};
    bypass_gst_poll_slot output_message_slot{};
};

/**
 * @brief Starts one deterministic worker set for the current lifecycle cycle.
 * @param data Shared runtime state.
 * @param options Message-thread start options.
 * @return Heap-allocated worker bundle (stable addresses for GstTask slots).
 */
[[nodiscard]] std::unique_ptr<worker_thread_bundle> start_worker_threads(global_data &data,
                                                                         const worker_thread_start_options &options);

/**
 * @brief Waits for one worker bundle to drain and join in deterministic order.
 * @param data Shared runtime state.
 * @param bundle Worker bundle created by start_worker_threads().
 */
void wait_for_worker_threads_completion(global_data &data, worker_thread_bundle &bundle);

/**
 * @brief Waits for file-mode workers, flushing encoded backlog before joining the encoded GstTask.
 * @param data Shared runtime state.
 * @param bundle Worker bundle created by start_worker_threads().
 */
void wait_for_file_mode_worker_threads_completion(global_data &data, worker_thread_bundle &bundle);

/**
 * @brief Starts bypass polling workers if bypass feature is enabled.
 *
 * Inference, bypass, and merger consumers use GstTask for bypass mode.
 *
 * @param data The global data structure.
 * @return false if bypass is enabled but workers could not be started; true if bypass disabled (no-op) or start OK.
 */
[[nodiscard]] bool start_bypass_polling_threads(global_data &data);

/**
 * @brief Ensures bypass polling worker threads are running when bypass is enabled.
 * @param data Shared runtime state.
 */
void restart_bypass_polling_threads_if_needed(global_data &data);

/**
 * @brief Joins bypass polling worker threads if they were started.
 * @param data Shared runtime state.
 */
void join_bypass_polling_threads(global_data &data);
