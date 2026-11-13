// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "frame_router.h"

#include "../../../../libvcai/include/logging.h"
#include "../../runtime/bypass_state.h"

/**
 * @file frame_router.cpp
 * @brief Implementation of frame routing logic for dual-path bypass mode.
 */

/// GStreamer debug category for frame routing.
GST_DEBUG_CATEGORY_STATIC(frame_router_cat);

namespace {

    /**
     * @brief Initializes the debug category for frame routing.
     */
    void init_frame_router_debug_category() {
        static bool initialized = false;
        if (!initialized) {
            GST_DEBUG_CATEGORY_INIT(frame_router_cat, "frame-router", 0, "Frame routing for dual-path bypass");
            initialized = true;
        }
    }

} // namespace

frame_router::frame_router(int interval) : bypass_interval(interval) {
    init_frame_router_debug_category();
    sf::info(frame_router_cat, "Frame router initialized: bypass_interval=%d", interval);
}

/**
 * @brief Returns routing decision based on bypass interval.
 * @return Routing decision for the next frame.
 */
routing_decision frame_router::decide_route() {
    // If bypass is disabled, always route to inference
    if (bypass_interval <= 0) {
        inference_count.fetch_add(1, std::memory_order_relaxed);
        return routing_decision::inference;
    }

    const auto frame_num = frame_counter.fetch_add(1, std::memory_order_relaxed);

    // bypass_interval = 2: frames 1, 3, 5, ... bypass (every 2nd frame)
    // bypass_interval = 3: frames 2, 5, 8, ... bypass (every 3rd frame)
    // bypass_interval = 4: frames 3, 7, 11, ... bypass (every 4th frame)
    //
    // Pattern: bypass when (frame_num + 1) % bypass_interval == 0
    //
    // This ensures the first frame always goes to inference (frame_num=0).

    if ((frame_num + 1) % static_cast<guint64>(bypass_interval) == 0) {
        bypass_count.fetch_add(1, std::memory_order_relaxed);
        return routing_decision::bypass;
    }

    inference_count.fetch_add(1, std::memory_order_relaxed);
    return routing_decision::inference;
}

/**
 * @brief Returns the configured bypass interval.
 */
int frame_router::get_bypass_interval() const { return bypass_interval; }

/**
 * @brief Returns number of frames routed to inference.
 */
guint64 frame_router::get_inference_frame_count() const { return inference_count.load(std::memory_order_relaxed); }

/**
 * @brief Returns number of frames routed to bypass.
 */
guint64 frame_router::get_bypass_frame_count() const { return bypass_count.load(std::memory_order_relaxed); }

void frame_router::reset() {
    frame_counter.store(0, std::memory_order_relaxed);
    inference_count.store(0, std::memory_order_relaxed);
    bypass_count.store(0, std::memory_order_relaxed);
    sf::info(frame_router_cat, "Frame router reset");
}

GstPadProbeReturn routing_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    (void) pad; // Unused

    auto *ctx = static_cast<routing_probe_context *>(user_data);
    if (!ctx || !ctx->bypass || !ctx->router) {
        return GST_PAD_PROBE_OK;
    }

    // Only process buffer probes
    if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
        return GST_PAD_PROBE_OK;
    }

    const auto decision = ctx->router->decide_route();

    // Control valves based on routing decision
    // CRITICAL: Must set both valves atomically before buffer passes through tee
    const gboolean inference_drop = (decision == routing_decision::bypass) ? TRUE : FALSE;
    const gboolean bypass_drop = (decision == routing_decision::bypass) ? FALSE : TRUE;
    g_object_set(ctx->bypass->valve_inference, "drop", inference_drop, nullptr);
    g_object_set(ctx->bypass->valve_bypass, "drop", bypass_drop, nullptr);

    return GST_PAD_PROBE_OK;
}

gulong install_routing_probe(GstElement *tee, routing_probe_context *context) {
    init_frame_router_debug_category();

    GstPad *sink_pad = gst_element_get_static_pad(tee, "sink");
    if (!sink_pad) {
        sf::error(frame_router_cat, "Failed to get tee sink pad for routing probe");
        return 0;
    }

    const gulong probe_id =
            gst_pad_add_probe(sink_pad, GST_PAD_PROBE_TYPE_BUFFER, routing_probe_callback, context, nullptr);

    gst_object_unref(sink_pad);

    if (probe_id == 0) {
        sf::error(frame_router_cat, "Failed to install routing probe on tee");
        return 0;
    }

    sf::info(frame_router_cat, "Routing probe installed on tee (probe_id=%lu)", probe_id);
    return probe_id;
}
