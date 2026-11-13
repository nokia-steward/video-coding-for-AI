// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ingress_bridge_core.h"

#include <gst/app/gstappsink.h>

namespace {
    [[nodiscard]] ingress_bridge_pull_result make_result(const ingress_bridge_step_status status,
                                                         sf::unique_gst_sample_ptr sample = {}) {
        return {.status = status, .sample = std::move(sample)};
    }
} // namespace

ingress_bridge_pull_result ingress_bridge_pull_sample_from_signal(GstElement *sink) {
    if (sink == nullptr) {
        return make_result(ingress_bridge_step_status::fatal_stop);
    }

    GstSample *sample = nullptr;
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample == nullptr) {
        return make_result(ingress_bridge_step_status::recoverable_drop);
    }

    return make_result(ingress_bridge_step_status::ok, sf::unique_gst_sample_ptr(sample));
}

ingress_bridge_pull_result ingress_bridge_try_pull_sample(GstElement *appsink, const GstClockTime timeout) {
    if (appsink == nullptr || !GST_IS_APP_SINK(appsink)) {
        return make_result(ingress_bridge_step_status::fatal_stop);
    }

    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), timeout);
    if (sample != nullptr) {
        return make_result(ingress_bridge_step_status::ok, sf::unique_gst_sample_ptr(sample));
    }

    if (gst_app_sink_is_eos(GST_APP_SINK(appsink))) {
        return make_result(ingress_bridge_step_status::drain_complete);
    }

    return make_result(ingress_bridge_step_status::recoverable_drop);
}
