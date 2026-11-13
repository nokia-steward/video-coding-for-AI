// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include "../../runtime/bypass_gst_poll_slot.h"

/**
 * @file appsink_polling.h
 * @brief Declares functions and structures for polling appsink elements in dual-path bypass mode.
 */

/**
 * @brief Forward declaration for shared application state (stable raw escape hatch for GStreamer tasks).
 */
struct global_data;
/**
 * @brief Forward declaration for the narrow input/ingress view used at the public seam.
 */
class input_ingress_facade;
/**
 * @brief Forward declaration for frame merger used by bypass routing.
 */
struct frame_merger;

/**
 * @brief Context for appsink polling threads.
 */
struct appsink_poll_context {
    GstElement *appsink = nullptr;
    frame_merger *merger = nullptr;
    global_data *data = nullptr;
    std::atomic<bool> *stop_flag = nullptr;
    std::atomic<int> *eos_counter = nullptr;
    bool is_inference_path = false;
};

/**
 * @brief Thread function that polls the inference appsink.
 *
 * Runs the full synchronous poll loop on the caller's thread (used by tests and legacy call sites).
 *
 * @param ctx Polling context containing appsink and merger references.
 */
void inference_appsink_poll_thread(appsink_poll_context ctx);

/**
 * @brief Thread function that polls the bypass appsink.
 *
 * Runs the full synchronous poll loop on the caller's thread (used by tests and legacy call sites).
 *
 * @param ctx Polling context containing appsink and merger references.
 */
void bypass_appsink_poll_thread(appsink_poll_context ctx);

/**
 * @brief Thread function that consumes frames from the merger.
 *
 * Pulls frames from the frame merger in PTS order and pushes them to the
 * raw_frame_queue for downstream processing.
 *
 * @param data Global data containing merger and queue.
 */
void merger_consumer_thread(input_ingress_facade &data);

/**
 * @brief Starts a GstTask that polls one bypass appsink (inference or bypass path).
 * @param slot Storage for the task and its lock (typically `bypass_state::*_poll_slot`).
 * @param ctx Polling context; copied into task user data.
 * @param path_name Logging label ("Inference" or "Bypass").
 * @return true if the task was created and started.
 */
[[nodiscard]] bool start_appsink_poll_gst_task(bypass_gst_poll_slot &slot, appsink_poll_context ctx,
                                               const char *path_name);

/**
 * @brief Stops and joins a bypass appsink GstTask, releasing the slot.
 */
void join_appsink_poll_gst_task(bypass_gst_poll_slot &slot);

/**
 * @brief Starts the GstTask that consumes merged frames into the ingress raw queue.
 */
[[nodiscard]] bool start_merger_poll_gst_task(input_ingress_facade &data, bypass_gst_poll_slot &slot);

/**
 * @brief Requests merger consumer shutdown, stops/joins the merger GstTask, releases the slot.
 */
void join_merger_poll_gst_task(input_ingress_facade &data, bypass_gst_poll_slot &slot);

/**
 * @brief Initializes the appsink polling debug category.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_appsink_polling_debug_category();
