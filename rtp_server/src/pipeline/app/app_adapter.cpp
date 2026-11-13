// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "app_adapter.h"

#include "../../client/client_session.h"
#include "../pipeline.h"
#include "logging.h"

#include <cstring>

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

namespace {
    [[nodiscard]] bool extract_rtp_sequence_and_timestamp(GstBuffer *buffer, guint16 &sequence_number,
                                                          guint32 &timestamp) {
        GstMapInfo map_info{};
        if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            return false;
        }

        const bool has_basic_rtp_header = map_info.size >= 12;
        if (!has_basic_rtp_header) {
            gst_buffer_unmap(buffer, &map_info);
            return false;
        }

        const auto *data = map_info.data;
        sequence_number = static_cast<guint16>((static_cast<guint16>(data[2]) << 8U) | static_cast<guint16>(data[3]));
        timestamp = (static_cast<guint32>(data[4]) << 24U) | (static_cast<guint32>(data[5]) << 16U) |
                    (static_cast<guint32>(data[6]) << 8U) | static_cast<guint32>(data[7]);

        gst_buffer_unmap(buffer, &map_info);
        return true;
    }

    gboolean stop_pipeline_on_main_context(gpointer user_data) {
        auto *pipeline = static_cast<rtp_pipeline *>(user_data);
        if (pipeline) {
            pipeline->stop_pipeline();
        }
        return G_SOURCE_REMOVE;
    }
} // namespace

pipeline_app_adapter::pipeline_app_adapter(rtp_pipeline &pipeline_ref) : pipeline(pipeline_ref) {}

bool pipeline_app_adapter::handle_rtp_metadata_probe_payload(GstBuffer *buffer) const {
    if (!buffer) {
        return request_fail_fast_stop("RTP metadata probe adapter received null buffer");
    }

    if (!pipeline.client_session_ptr) {
        return true;
    }

    const auto buffer_size = static_cast<guint32>(gst_buffer_get_size(buffer));
    if (pipeline.draw_bitrate || pipeline.draw_bitrate_large) {
        pipeline.stats_context.statistics.incoming_bitrate_tracker.add_buffer_sample(buffer,
                                                                                     static_cast<guint64>(buffer_size));
    }

    pipeline.client_session_ptr->update_activity();
    sf::debug(pipeline_category, "RTP metadata probe observed payload on port %u: size=%u", pipeline.get_port(),
              buffer_size);
    return true;
}

bool pipeline_app_adapter::handle_pad_probe_buffer(GstElement *parent_element, GstBuffer *buffer) const {
    if (!parent_element || !buffer) {
        return request_fail_fast_stop("Pad probe adapter received null parent element or buffer");
    }

    auto &stats = pipeline.stats_context.statistics;
    const auto buffer_size = static_cast<guint32>(gst_buffer_get_size(buffer));

    if (parent_element == pipeline.rtp_input_source.get()) {
        ++stats.rtp_input_packets_received;
        stats.rtp_input_bytes_received += buffer_size;

        if (pipeline.client_session_ptr) {
            guint16 sequence_number = 0;
            guint32 timestamp = 0;
            if (extract_rtp_sequence_and_timestamp(buffer, sequence_number, timestamp)) {
                pipeline.client_session_ptr->update_rtp_statistics(buffer_size, timestamp, sequence_number);
            } else {
                pipeline.client_session_ptr->update_rtp_statistics_without_metadata(buffer_size);
            }
        }

        sf::debug(pipeline_category, "RTP input packet received on port %u: %u bytes", pipeline.get_port(),
                  buffer_size);
        return true;
    }

    if (parent_element == pipeline.rtph265depay.get()) {
        ++stats.rtp_packets_depayloaded;
        sf::debug(pipeline_category, "RTP packet depayloaded on port %u: %u bytes", pipeline.get_port(), buffer_size);
        return true;
    }

    if (parent_element == pipeline.h265parse.get()) {
        ++stats.h265_frames_parsed;
        stats.h265_bytes_parsed += buffer_size;
        sf::debug(pipeline_category, "H.265 frame parsed on port %u: %u bytes", pipeline.get_port(), buffer_size);
        return true;
    }

    if (parent_element == pipeline.filesink.get()) {
        stats.file_bytes_written += buffer_size;
        sf::debug(pipeline_category, "File written on port %u: %u bytes", pipeline.get_port(), buffer_size);
        return true;
    }

    if (parent_element == pipeline.rtp_output_sink.get()) {
        ++stats.rtp_output_packets_sent;
        stats.rtp_output_bytes_sent += buffer_size;
        sf::debug(pipeline_category, "RTP output packet sent on port %u: %u bytes to %s:%u", pipeline.get_port(),
                  buffer_size, pipeline.rtp_output.host.c_str(), pipeline.rtp_output.port);
        return true;
    }

    return true;
}

bool pipeline_app_adapter::update_decoded_session_statistics(const guint32 frame_size) const {
    if (!pipeline.client_session_ptr) {
        return true;
    }

    pipeline.client_session_ptr->update_decoded_frame_statistics(frame_size);
    return true;
}

bool pipeline_app_adapter::request_fail_fast_stop(const char *failure_reason) const {
    const char *const reason = (failure_reason && std::strlen(failure_reason) > 0) ? failure_reason : "unspecified";
    sf::error(pipeline_category, "Fail-fast stop requested for port %u: %s", pipeline.port, reason);

    pipeline.runtime_context.notify_state_change();

    if (!pipeline.runtime_context.pipeline_stopped.load() &&
        !pipeline.runtime_context.pipeline_stop_in_progress.load()) {
        g_main_context_invoke(nullptr, stop_pipeline_on_main_context, &pipeline);
    }

    return false;
}
