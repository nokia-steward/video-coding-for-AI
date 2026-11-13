// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "callbacks.h"
#include "logging.h"

#include <gst/gst.h>

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

gboolean on_pipeline_message(GstBus *bus [[maybe_unused]], GstMessage *message, gpointer user_data) {
    const auto *transport_context = static_cast<transport_build_context *>(user_data);
    if (!transport_context) {
        return TRUE;
    }
    const guint16 port = transport_context->port;

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *debug_info = nullptr;
            gst_message_parse_error(message, &err, &debug_info);
            // Check if this is a window close error (treat as EOS)
            if (g_strrstr(err->message, "Output window was closed") != nullptr) {
                sf::info(pipeline_category, "Output window was closed for port %u. Treating as EOS.", port);
                g_clear_error(&err);
                g_free(debug_info);
                // Fall through to EOS handling
            } else {
                sf::error(pipeline_category, "ERROR from pipeline on port %u: %s", port, err->message);
                sf::error(pipeline_category, "Debug info: %s", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
            }
            break;
        }
        case GST_MESSAGE_EOS: {
            sf::info(pipeline_category, "End-of-stream on port %u. Pipeline will be cleaned up by client manager.",
                     port);
            break;
        }
        default:
            break;
    }

    return TRUE; // Continue watching for messages
}
