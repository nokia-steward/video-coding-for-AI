// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "shutdown.h"

#include "../pipeline.h"
#include "logging.h"

#include <gst/gst.h>

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

void disconnect_signal_handlers(rtp_pipeline &pipeline) {
    const guint16 port = pipeline.get_port();
    sf::info(pipeline_category, "Disconnecting signal handlers for port %u...", port);
    for (const auto &handler_info: pipeline.runtime_context.signal_handler_infos) {
        if (handler_info.element) {
            g_signal_handler_disconnect(handler_info.element, handler_info.handler_id);
            sf::debug(pipeline_category, "Disconnected signal handler %lu for port %u", handler_info.handler_id, port);
        }
    }
    pipeline.runtime_context.signal_handler_infos.clear();

    pipeline.runtime_context.webrtc_signal_data_ptr.reset();
}

void stop_single_pipeline(sf::unique_gst_element_ptr &pipeline_element) {
    if (pipeline_element) {
        const GstStateChangeReturn ret = gst_element_set_state(pipeline_element.get(), GST_STATE_NULL);
        if (ret == GST_STATE_CHANGE_ASYNC) {
            GstState current_state = GST_STATE_NULL;
            GstState pending = GST_STATE_NULL;
            gst_element_get_state(pipeline_element.get(), &current_state, &pending, 2 * GST_SECOND);
        }
        pipeline_element.reset();
    }
}

void stop_all_pipelines(rtp_pipeline &pipeline) { stop_single_pipeline(pipeline.main_pipeline); }
