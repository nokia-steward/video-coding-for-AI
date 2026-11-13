// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "pipeline_message_processing.h"
#include <gst/gst.h>
#include <string>
#include "../../libvcai/include/logging.h"
#include "gst_raii.h"

/**
 * @file pipeline_message_processing.cpp
 * @brief GStreamer pipeline message bus loop and optional GstTask worker.
 */

/// GStreamer debug category for pipeline message processing.
GST_DEBUG_CATEGORY_STATIC(pipeline_message_processing);

void init_pipeline_message_processing_debug_category() {
    GST_DEBUG_CATEGORY_INIT(pipeline_message_processing, "pipeline-message-processing", 0,
                            "Processing of GStreamer pipeline messages and bus events");
}

namespace {

    /**
     * @brief One bus pop + dispatch; returns false when the outer loop / task should stop.
     */
    [[nodiscard]] bool pipeline_messages_iteration(GstElement *pipeline_el, global_data &data, GstBus *bus,
                                                   bool &keep_running) {
        if (data.stop_processing) {
            return false;
        }

        auto msg = sf::unique_gst_message_ptr(gst_bus_timed_pop_filtered(
                bus, 50 * GST_MSECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING |
                                            GST_MESSAGE_NEED_CONTEXT | GST_MESSAGE_HAVE_CONTEXT)));

        if (msg == nullptr) {
            return !data.stop_processing;
        }

        switch (GST_MESSAGE_TYPE(msg.get())) {
        case GST_MESSAGE_ERROR: {
            GError *err_raw = nullptr;
            gchar *dbg_info_raw = nullptr;
            gst_message_parse_error(msg.get(), &err_raw, &dbg_info_raw);
            const auto err = sf::unique_g_error_ptr(err_raw);
            const auto dbg_info = sf::unique_g_char_ptr(dbg_info_raw);
            sf::error(pipeline_message_processing, "ERROR from element %s: %s", GST_OBJECT_NAME(msg->src),
                      err->message);
            sf::error(pipeline_message_processing, "Debugging info: %s", dbg_info.get() ? dbg_info.get() : "none");

            const std::string element_name = GST_OBJECT_NAME(msg->src);

            if (const std::string error_message = err->message;
                element_name.find("s-disp") != std::string::npos &&
                (error_message.find("Output window was closed") != std::string::npos ||
                 error_message.find("no window") != std::string::npos)) {
                sf::warning(pipeline_message_processing, "Display window was closed. Handling gracefully.");

                if (!data.handle_display_window_closed()) {
                    sf::info(pipeline_message_processing,
                             "Display was the only output branch. Initiating graceful shutdown.");
                    data.shutdown();
                } else {
                    sf::info(pipeline_message_processing,
                             "Other output branches are still active. Continuing processing.");
                }
            } else {
                if (data.monitoring.status_reporter) {
                    data.monitoring.status_reporter->report_error();
                }

                data.shutdown();
            }
            break;
        }
        case GST_MESSAGE_EOS:
            sf::info(pipeline_message_processing, "EOS received on pipeline %s", GST_OBJECT_NAME(pipeline_el));
            if (pipeline_el == data.input_pipeline.get()) {
                sf::info(pipeline_message_processing, "Input pipeline EOS: closing raw frame queue");
                data.close_raw_queue_once();
                keep_running = false;
                break;
            }

            if (pipeline_el == data.encoding_pipeline.get()) {
                sf::info(pipeline_message_processing, "Encoding pipeline EOS: closing encoded frame queue");
                data.close_encoded_queue_once();
                keep_running = false;
                break;
            }

            if (pipeline_el == data.output_pipeline.get()) {
                data.shutdown();
            }
            break;
        case GST_MESSAGE_WARNING: {
            GError *err_raw = nullptr;
            gchar *dbg_info_raw = nullptr;
            gst_message_parse_warning(msg.get(), &err_raw, &dbg_info_raw);
            const auto err = sf::unique_g_error_ptr(err_raw);
            const auto dbg_info = sf::unique_g_char_ptr(dbg_info_raw);
            sf::warning(pipeline_message_processing, "WARNING from element %s: %s", GST_OBJECT_NAME(msg->src),
                        err->message);
            sf::warning(pipeline_message_processing, "Debugging info: %s",
                        dbg_info.get() ? dbg_info.get() : "none");
            break;
        }
        case GST_MESSAGE_NEED_CONTEXT: {
            const gchar *context_type = nullptr;
            gst_message_parse_context_type(msg.get(), &context_type);
            sf::warning(pipeline_message_processing, "NEED_CONTEXT from element %s: type=%s",
                        GST_OBJECT_NAME(msg->src), context_type ? context_type : "unknown");
            break;
        }
        case GST_MESSAGE_HAVE_CONTEXT: {
            GstContext *context = nullptr;
            gst_message_parse_have_context(msg.get(), &context);
            const char *context_type = context ? gst_context_get_context_type(context) : "unknown";
            sf::info(pipeline_message_processing, "HAVE_CONTEXT from element %s: type=%s",
                     GST_OBJECT_NAME(msg->src), context_type ? context_type : "unknown");
            if (context) {
                gst_context_unref(context);
            }
            break;
        }
        default:
            break;
        }

        return keep_running && !data.stop_processing;
    }

    struct pipeline_message_task_userdata {
        global_data *data = nullptr;
        GstElement *pipeline = nullptr;
        GstBus *bus = nullptr;
        bool keep_running = true;
        GstTask *self_task = nullptr;
    };

    void pipeline_message_task_entry(gpointer user_data) {
        auto *ud = static_cast<pipeline_message_task_userdata *>(user_data);
        if (!pipeline_messages_iteration(ud->pipeline, *ud->data, ud->bus, ud->keep_running)) {
            gst_task_stop(GST_TASK(ud->self_task));
            return;
        }
        if (!ud->keep_running) {
            gst_task_stop(GST_TASK(ud->self_task));
        }
    }

    void destroy_pipeline_message_task_userdata(gpointer user_data) {
        auto *ud = static_cast<pipeline_message_task_userdata *>(user_data);
        if (ud->bus) {
            gst_object_unref(ud->bus);
        }
        if (ud->pipeline) {
            gst_object_unref(ud->pipeline);
        }
        delete ud;
    }

} // namespace

void process_pipeline_messages(const sf::unique_gst_element_ptr &pipeline, global_data &data) {
    const auto bus = sf::unique_gst_bus_ptr(gst_element_get_bus(pipeline.get()));
    bool keep_running = true;

    while (!data.stop_processing && keep_running) {
        if (!pipeline_messages_iteration(pipeline.get(), data, bus.get(), keep_running)) {
            break;
        }
    }
}

bool start_pipeline_message_gst_task(global_data &data, GstElement *pipeline, bypass_gst_poll_slot &slot) {
    if (slot.task != nullptr || pipeline == nullptr) {
        return false;
    }

    // RAII_EXEMPT: GstTask user_data; destroyed in destroy_pipeline_message_task_userdata.
    auto *ud = new pipeline_message_task_userdata{};
    ud->data = &data;
    ud->pipeline = GST_ELEMENT(gst_object_ref(pipeline));
    ud->bus = gst_element_get_bus(ud->pipeline);
    if (!ud->bus) {
        gst_object_unref(ud->pipeline);
        delete ud;
        sf::error(pipeline_message_processing, "Failed to get GstBus for pipeline message task");
        return false;
    }

    GstTask *const task = gst_task_new(pipeline_message_task_entry, ud, destroy_pipeline_message_task_userdata);
    if (!task) {
        gst_object_unref(ud->bus);
        gst_object_unref(ud->pipeline);
        delete ud;
        sf::error(pipeline_message_processing, "Failed to create GstTask for pipeline messages");
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &slot.lock);
    if (!gst_task_start(task)) {
        sf::error(pipeline_message_processing, "Failed to start GstTask for pipeline messages");
        gst_object_unref(task);
        return false;
    }

    slot.task = task;
    return true;
}

void join_pipeline_message_gst_task(bypass_gst_poll_slot &slot) {
    if (slot.task == nullptr) {
        return;
    }
    GstTask *const task = slot.task;
    slot.task = nullptr;

    gst_task_stop(task);
    if (!gst_task_join(task)) {
        sf::warning(pipeline_message_processing, "gst_task_join failed for pipeline message task");
    }
    gst_object_unref(task);
}
