// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "gst_callbacks.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include "../../libvcai/include/logging.h"
#include "../bridge/app/ingress_app_adapter.h"
#include "../bridge/context/ingress_bridge_context.h"
#include "../bridge/orchestration/ingress_bridge_orchestrator.h"
#include "../encoder_enums.h"

/**
 * @file gst_callbacks.cpp
 * @brief Implementation of GStreamer signal callback functions.
 */

/// GStreamer debug category for callback handling.
GST_DEBUG_CATEGORY_STATIC(gst_callbacks);

void init_gst_callbacks_debug_category() {
    GST_DEBUG_CATEGORY_INIT(gst_callbacks, "gst-callbacks", 0, "GStreamer callback functions for signal handling");
}

GstFlowReturn on_new_raw_sample(GstElement *sink, global_data *data) {
    if (data == nullptr) {
        sf::error(gst_callbacks, "on_new_raw_sample called with null global_data");
        return GST_FLOW_ERROR;
    }

    auto context = ingress_bridge_context{.runtime = &data->ingress_runtime};
    const ingress_app_adapter app_adapter(*data);
    return ingress_handle_raw_new_sample_via_orchestrator(context, app_adapter, sink, gst_callbacks);
}

void on_raw_eos(GstElement *, global_data *data) {
    if (data == nullptr) {
        sf::error(gst_callbacks, "on_raw_eos called with null global_data");
        return;
    }

    auto context = ingress_bridge_context{.runtime = &data->ingress_runtime};
    const ingress_app_adapter app_adapter(*data);
    ingress_handle_raw_eos_via_orchestrator(context, app_adapter, gst_callbacks);
}

GstFlowReturn on_new_encoded_sample(GstElement *sink, global_data *data) {
    if (data == nullptr) {
        sf::error(gst_callbacks, "on_new_encoded_sample called with null global_data");
        return GST_FLOW_ERROR;
    }

    auto context = ingress_bridge_context{.runtime = &data->ingress_runtime};
    const ingress_app_adapter app_adapter(*data);
    return ingress_handle_encoded_new_sample_via_orchestrator(context, app_adapter, sink, gst_callbacks);
}

void on_encoded_eos(GstElement *, global_data *data) {
    if (data == nullptr) {
        sf::error(gst_callbacks, "on_encoded_eos called with null global_data");
        return;
    }

    auto context = ingress_bridge_context{.runtime = &data->ingress_runtime};
    const ingress_app_adapter app_adapter(*data);
    ingress_handle_encoded_eos_via_orchestrator(context, app_adapter, gst_callbacks);
}
