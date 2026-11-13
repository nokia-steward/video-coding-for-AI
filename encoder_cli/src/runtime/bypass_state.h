// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../gst/input_types/frame_merger.h"
#include "../gst/input_types/frame_router.h"
#include "../gst/input_types/last_inference_roi.h"
#include "bypass_gst_poll_slot.h"

#include <atomic>
#include <gst/gst.h>
#include <memory>

/**
 * @file bypass_state.h
 * @brief Groups bypass-specific runtime state.
 */

/**
 * @brief Groups bypass-specific runtime state.
 */
struct bypass_state {
    bypass_state() :
        merger(nullptr), appsink_inference(nullptr), appsink_bypass(nullptr), valve_bypass(nullptr),
        valve_inference(nullptr), appsink_eos_count(0), router(nullptr), last_inference_roi(), routing_probe_id(0),
        routing_ctx(nullptr), inference_poll_slot(), bypass_poll_slot(), merger_poll_slot() {}

    bypass_state(const bypass_state &) = delete;
    bypass_state &operator=(const bypass_state &) = delete;
    bypass_state(bypass_state &&) = delete;
    bypass_state &operator=(bypass_state &&) = delete;

    std::unique_ptr<frame_merger> merger;
    GstElement *appsink_inference = nullptr;
    GstElement *appsink_bypass = nullptr;
    GstElement *valve_bypass = nullptr;
    GstElement *valve_inference = nullptr;
    std::atomic<int> appsink_eos_count{0};
    std::unique_ptr<frame_router> router;
    last_inference_roi_data last_inference_roi;
    gulong routing_probe_id = 0;
    std::unique_ptr<routing_probe_context> routing_ctx;
    bypass_gst_poll_slot inference_poll_slot;
    bypass_gst_poll_slot bypass_poll_slot;
    /// Merger consumer runs as GstTask; `frame_merger::request_consumer_shutdown()` unblocks `get_next_frame()` on
    /// join.
    bypass_gst_poll_slot merger_poll_slot;
};
