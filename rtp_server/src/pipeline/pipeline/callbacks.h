// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include <gst/gst.h>

#include "../context/pipeline_contexts.h"

/**
 * @brief Bus message handler callback.
 * @param bus The bus that emitted the message.
 * @param message The message.
 * @param user_data Pointer to transport build context.
 * @return TRUE to continue watching for messages.
 */
gboolean on_pipeline_message(GstBus *bus, GstMessage *message, gpointer user_data);
