// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "probes.h"
#include "../decoded_frame_pipeline.h"
#include "../pipeline.h"
#include "logging.h"

#include <gst/gst.h>

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

GstPadProbeReturn rtp_metadata_probe_callback(GstPad *, GstPadProbeInfo *info, gpointer user_data) {
    auto *pipeline = static_cast<rtp_pipeline *>(user_data);

    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        if (auto buffer = GST_PAD_PROBE_INFO_BUFFER(info); buffer) {
            if (!pipeline->app_adapter.handle_rtp_metadata_probe_payload(buffer)) {
                sf::error(pipeline_category, "RTP metadata probe adapter failure on port %u", pipeline->get_port());
                return GST_PAD_PROBE_DROP;
            }
        }
    }

    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn pad_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    auto *pipeline = static_cast<rtp_pipeline *>(user_data);
    const sf::unique_gst_element_ptr parent_element(gst_pad_get_parent_element(pad));
    if (!parent_element) {
        return GST_PAD_PROBE_OK;
    }

    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }

        if (!pipeline->app_adapter.handle_pad_probe_buffer(parent_element.get(), buffer)) {
            sf::error(pipeline_category, "Pad probe adapter failure on port %u", pipeline->get_port());
            return GST_PAD_PROBE_DROP;
        }
    }

    return GST_PAD_PROBE_OK;
}

namespace {
    void add_probe_to_pad(GstElement *element, const char *pad_name, GstPadProbeType probe_type,
                          GstPadProbeCallback callback, gpointer user_data, guint16 port, const char *pad_type,
                          const char *description) {
        if (!element) {
            return;
        }

        if (const auto pad = sf::unique_gst_pad_ptr(gst_element_get_static_pad(element, pad_name))) {
            gst_pad_add_probe(pad.get(), probe_type, callback, user_data, nullptr);
            sf::debug(pipeline_category, "Added %s probe to %s %s pad for port %u", description, pad_name, pad_type,
                      port);
        }
    }

    GstPadProbeReturn encoded_h265_stream_statistics_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        auto *pipeline = static_cast<rtp_pipeline *>(user_data);
        if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }
        auto &stats = pipeline->stats_context.statistics;
        ++stats.appsink_samples_received;
        ++stats.appsrc_samples_pushed;
        const auto sz = static_cast<guint32>(gst_buffer_get_size(buffer));
        stats.appsink_bytes_received += sz;
        stats.appsrc_bytes_pushed += sz;

        if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
            sf::unique_gst_caps_no_delete_ptr cw(caps);
            extract_video_properties(*pipeline, cw);
            gst_caps_unref(caps);
        }
        return GST_PAD_PROBE_OK;
    }

    GstPadProbeReturn post_tet_statistics_probe(GstPad *, GstPadProbeInfo *info, gpointer user_data) {
        auto *pipeline = static_cast<rtp_pipeline *>(user_data);
        if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }
        auto &stats = pipeline->stats_context.statistics;
        ++stats.decoded_frames_received;
        stats.decoded_bytes_received += static_cast<guint32>(gst_buffer_get_size(buffer));
        return GST_PAD_PROBE_OK;
    }

    GstPadProbeReturn display_path_statistics_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        auto *pipeline = static_cast<rtp_pipeline *>(user_data);
        if (!(info->type & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) {
            return GST_PAD_PROBE_OK;
        }
        const auto sz = static_cast<guint32>(gst_buffer_get_size(buffer));
        auto &stats = pipeline->stats_context.statistics;
        ++stats.decoded_appsrc_samples_pushed;
        stats.decoded_appsrc_bytes_pushed += sz;
        ++stats.display_frames_sent;

        if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
            sf::unique_gst_caps_no_delete_ptr cw(caps);
            extract_video_properties(*pipeline, cw);
            sync_nvstreammux_dimensions_if_needed(*pipeline);
            gst_caps_unref(caps);
        }

        (void) pipeline->app_adapter.update_decoded_session_statistics(sz);
        return GST_PAD_PROBE_OK;
    }

    void add_input_pipeline_probes(GstElement *rtp_input_source, GstElement *rtph265depay, GstElement *h265parse,
                                   GstElement *h265_capsfilter, rtp_pipeline *pipeline, guint16 port) {
        add_probe_to_pad(rtp_input_source, "src", GST_PAD_PROBE_TYPE_BUFFER, pad_probe_callback, pipeline, port, "src",
                         "buffer");

        add_probe_to_pad(rtph265depay, "src", GST_PAD_PROBE_TYPE_BUFFER, rtp_metadata_probe_callback, pipeline, port,
                         "src", "RTP metadata");

        add_probe_to_pad(h265parse, "src", GST_PAD_PROBE_TYPE_BUFFER, pad_probe_callback, pipeline, port, "src",
                         "buffer");

        add_probe_to_pad(h265_capsfilter, "src", GST_PAD_PROBE_TYPE_BUFFER, encoded_h265_stream_statistics_probe,
                         pipeline, port, "src", "encoded byte-stream statistics");
    }

    void add_output_pipeline_probes(GstElement *filesink, bool rtp_output_enabled, GstElement *rtp_output_sink,
                                    rtp_pipeline *pipeline, guint16 port) {
        add_probe_to_pad(filesink, "sink", GST_PAD_PROBE_TYPE_BUFFER, pad_probe_callback, pipeline, port, "sink",
                         "buffer");

        if (rtp_output_enabled) {
            add_probe_to_pad(rtp_output_sink, "sink", GST_PAD_PROBE_TYPE_BUFFER, pad_probe_callback, pipeline, port,
                             "sink", "buffer");
        }
    }

} // namespace

void add_pad_probes(rtp_pipeline &pipeline) {
    const guint16 port = pipeline.get_port();
    sf::info(pipeline_category, "Adding pad probes for data flow monitoring on port %u", port);

    add_input_pipeline_probes(pipeline.rtp_input_source.get(), pipeline.rtph265depay.get(), pipeline.h265parse.get(),
                              pipeline.h265_capsfilter.get(), &pipeline, port);

    add_probe_to_pad(pipeline.sf_tet_apply.get(), "src", GST_PAD_PROBE_TYPE_BUFFER, post_tet_statistics_probe,
                     &pipeline, port, "src", "post-TET decoded statistics");

    add_probe_to_pad(pipeline.decoded_output_capsfilter.get(), "src", GST_PAD_PROBE_TYPE_BUFFER,
                     display_path_statistics_probe, &pipeline, port, "src", "display path statistics");

    add_output_pipeline_probes(pipeline.filesink.get(), pipeline.rtp_output.enabled, pipeline.rtp_output_sink.get(),
                               &pipeline, port);
    sf::info(pipeline_category, "Successfully added pad probes for port %u", port);
}
