// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <string.h>

typedef struct {
    gchar *input_path;
    gchar *tet_file;
    gchar *plugin_dir;
    gchar *decoder_element;
    gchar *sink_element;
    gint qp;
} app_options;

static gboolean parse_options(gint *argc, gchar ***argv, app_options *options) {
    GOptionContext *context = nullptr;
    GOptionEntry entries[] = {
            {"input", 'i', 0, G_OPTION_ARG_FILENAME, &options->input_path, "Input H.265 Annex-B file", "PATH"},
            {"tet-file", 't', 0, G_OPTION_ARG_FILENAME, &options->tet_file, "Optional TET tile file (.tet)", "PATH"},
            {"plugin-dir", 'p', 0, G_OPTION_ARG_FILENAME, &options->plugin_dir,
             "Optional directory containing libgstvcai_decoder.so", "PATH"},
            {"decoder", 'd', 0, G_OPTION_ARG_STRING, &options->decoder_element,
             "Decoder element (default: avdec_h265, use decodebin for auto decode)", "ELEMENT"},
            {"sink", 's', 0, G_OPTION_ARG_STRING, &options->sink_element, "Sink element (default: fakesink)",
             "ELEMENT"},
            {"qp", 'q', 0, G_OPTION_ARG_INT, &options->qp, "QP for TET tile selection (default: 27)", "QP"},
            {nullptr, 0, 0, static_cast<GOptionArg>(0), nullptr, nullptr, nullptr}};
    GError *error = nullptr;
    gboolean ok = FALSE;

    context = g_option_context_new("- VCAI decoder split-element test app");
    g_option_context_add_main_entries(context, entries, nullptr);

    ok = g_option_context_parse(context, argc, argv, &error);
    if (!ok) {
        g_printerr("Failed to parse options: %s\n", error != nullptr ? error->message : "unknown error");
        g_clear_error(&error);
        g_option_context_free(context);
        return FALSE;
    }
    g_option_context_free(context);

    if (options->input_path == nullptr || options->input_path[0] == '\0') {
        g_printerr("Missing required --input argument.\n");
        return FALSE;
    }
    if (options->sink_element == nullptr || options->sink_element[0] == '\0') {
        options->sink_element = g_strdup("fakesink");
    }
    if (options->decoder_element == nullptr || options->decoder_element[0] == '\0') {
        options->decoder_element = g_strdup("avdec_h265");
    }
    if (options->qp < 0 || options->qp > 63) {
        g_printerr("Invalid --qp value %d (allowed range: 0..63)\n", options->qp);
        return FALSE;
    }

    return TRUE;
}

static gchar *build_pipeline_description(const app_options *options) {
    gchar *input_escaped = nullptr;
    gchar *tet_escaped = nullptr;
    gchar *decoder_escaped = nullptr;
    gchar *tet_stage = nullptr;
    gchar *description = nullptr;

    input_escaped = g_strescape(options->input_path, nullptr);
    if (input_escaped == nullptr) {
        return nullptr;
    }

    if (options->tet_file != nullptr && options->tet_file[0] != '\0') {
        tet_escaped = g_strescape(options->tet_file, nullptr);
        if (tet_escaped == nullptr) {
            g_free(input_escaped);
            return nullptr;
        }
        tet_stage =
                g_strdup_printf("sftetapply enabled=true tile-file=\"%s\" qp=%u dynamic-qp=false strict-apply=false",
                                tet_escaped, options->qp);
    } else {
        tet_stage = g_strdup("sftetapply enabled=false");
    }

    if (tet_stage != nullptr) {
        decoder_escaped = g_strescape(options->decoder_element, nullptr);
        if (decoder_escaped == nullptr) {
            g_free(input_escaped);
            g_free(tet_escaped);
            g_free(tet_stage);
            return nullptr;
        }

        description = g_strdup_printf(
                "filesrc location=\"%s\" ! h265parse ! sfh265seiextract ! %s ! videoconvert ! "
                "video/x-raw,format=I420 ! sfprireconstruct strict-parsing=false strict-reconstruction=false ! "
                "sfeoilumabackscale enabled=true strict-parsing=false ! %s ! %s sync=false",
                input_escaped, decoder_escaped, tet_stage, options->sink_element);
    }

    g_free(input_escaped);
    g_free(tet_escaped);
    g_free(decoder_escaped);
    g_free(tet_stage);
    return description;
}

static gint run_pipeline(const gchar *pipeline_description) {
    GstElement *pipeline = nullptr;
    GstBus *bus = nullptr;
    GstMessage *message = nullptr;
    GError *error = nullptr;
    gint exit_code = 1;

    pipeline = gst_parse_launch(pipeline_description, &error);
    if (pipeline == nullptr) {
        g_printerr("Failed to create pipeline: %s\n", error != nullptr ? error->message : "unknown error");
        g_clear_error(&error);
        return 1;
    }

    g_print("Running pipeline:\n%s\n", pipeline_description);
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to set pipeline to PLAYING.\n");
        gst_object_unref(pipeline);
        return 1;
    }

    bus = gst_element_get_bus(pipeline);
    if (bus == nullptr) {
        g_printerr("Failed to get pipeline bus.\n");
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 1;
    }

    message = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                         static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    if (message != nullptr) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            gchar *debug_info = nullptr;
            GError *pipeline_error = nullptr;

            gst_message_parse_error(message, &pipeline_error, &debug_info);
            g_printerr("Pipeline error: %s\n", pipeline_error != nullptr ? pipeline_error->message : "unknown error");
            if (debug_info != nullptr) {
                g_printerr("Debug info: %s\n", debug_info);
            }
            if (debug_info != nullptr && g_strstr_len(debug_info, -1, "not-linked") != nullptr) {
                g_printerr("Hint: try --decoder avdec_h265 (software decode) when decodebin/nv decode negotiation "
                           "fails.\n");
            }
            g_clear_error(&pipeline_error);
            g_free(debug_info);
            exit_code = 1;
        } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
            g_print("Pipeline reached EOS successfully.\n");
            exit_code = 0;
        }
        gst_message_unref(message);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    return exit_code;
}

int main(int argc, char **argv) {
    app_options options = {0};
    gchar *pipeline_description = nullptr;
    gint exit_code = 1;

    options.qp = 27;

    if (!parse_options(&argc, &argv, &options)) {
        goto cleanup;
    }

    if (options.plugin_dir != nullptr && options.plugin_dir[0] != '\0') {
        const gchar *existing_plugin_path = g_getenv("GST_PLUGIN_PATH");
        if (existing_plugin_path != nullptr && existing_plugin_path[0] != '\0') {
            gchar *combined_plugin_path = g_strdup_printf("%s:%s", options.plugin_dir, existing_plugin_path);
            if (combined_plugin_path != nullptr) {
                g_setenv("GST_PLUGIN_PATH", combined_plugin_path, TRUE);
            }
            g_free(combined_plugin_path);
        } else {
            g_setenv("GST_PLUGIN_PATH", options.plugin_dir, TRUE);
        }
    }

    gst_init(&argc, &argv);

    pipeline_description = build_pipeline_description(&options);
    if (pipeline_description == nullptr) {
        g_printerr("Failed to build pipeline description.\n");
        goto cleanup;
    }

    exit_code = run_pipeline(pipeline_description);

cleanup:
    g_free(pipeline_description);
    g_free(options.input_path);
    g_free(options.tet_file);
    g_free(options.plugin_dir);
    g_free(options.decoder_element);
    g_free(options.sink_element);
    return exit_code;
}
