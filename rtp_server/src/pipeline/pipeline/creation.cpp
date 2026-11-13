// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "creation.h"
#include "../../gst/gpu_elements.h"
#include "../pipeline.h"
#include "logging.h"
#include "utils.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <memory>
#include "communication/signaling.h"

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

/**
 * @brief Links the WebRTC src pad to the RTP capsfilter.
 * @param webrtcbin WebRTC bin that emitted the pad-added signal.
 * @param pad Newly created pad from the webrtcbin element.
 * @param user_data Pointer to webrtc_signal_data for this client.
 */
void on_webrtc_pad_added(GstElement *webrtcbin, GstPad *pad, gpointer user_data) {
    auto *data = static_cast<webrtc_signal_data *>(user_data);
    (void) webrtcbin; // Unused - required by signal signature

    if (!pad || !data || !data->build_context) {
        return;
    }

    // Check if this is a src pad (output pad from webrtcbin)
    const GstPadDirection direction = gst_pad_get_direction(pad);
    if (direction != GST_PAD_SRC) {
        return; // Not a src pad, ignore
    }

    // Get the pad name to check if it's the video pad
    const gchar *pad_name = gst_pad_get_name(pad);
    if (!pad_name) {
        return;
    }

    // Link webrtcbin pad to capsfilter

    // Get the webrtc_rtp_capsfilter element - this enforces H265 caps with clock-rate
    if (!data->build_context->webrtc_rtp_capsfilter) {
        sf::error(pipeline_category, "webrtc_rtp_capsfilter not created for port %u", data->build_context->port);
        return;
    }

    // Get the sink pad of webrtc_rtp_capsfilter
    GstPad *capsfilter_sink = gst_element_get_static_pad(data->build_context->webrtc_rtp_capsfilter.get(), "sink");
    if (!capsfilter_sink) {
        sf::error(pipeline_category, "Failed to get webrtc_rtp_capsfilter sink pad for port %u",
                  data->build_context->port);
        return;
    }

    // Link the webrtcbin src pad to capsfilter sink pad
    const GstPadLinkReturn link_ret = gst_pad_link(pad, capsfilter_sink);
    gst_object_unref(capsfilter_sink);

    if (link_ret != GST_PAD_LINK_OK) {
        sf::error(pipeline_category, "Failed to link webrtcbin pad %s to webrtc_rtp_capsfilter for port %u", pad_name,
                  data->build_context->port);
    } else {
        sf::info(pipeline_category, "Successfully linked webrtcbin pad %s to webrtc_rtp_capsfilter for port %u",
                 pad_name, data->build_context->port);
    }
}

namespace {

    /**
     * @brief Callback for webrtcbin "on-negotiation-needed" signal.
     * Creates and sends SDP answer.
     */
    /**
     * @brief Handles create-answer promise reply.
     * @param promise Promise containing the reply.
     * @param user_data webrtc_signal_data pointer.
     */
    void handle_create_answer_reply(GstPromise *promise, gpointer user_data) {
        auto *data = static_cast<webrtc_signal_data *>(user_data);
        sf::info(pipeline_category, "create-answer promise callback invoked for client %s", data->client_id.c_str());

        const GstStructure *reply = gst_promise_get_reply(promise);
        if (!reply) {
            sf::error(pipeline_category, "create-answer promise reply is null");
            gst_promise_unref(promise);
            return;
        }

        if (gst_structure_has_name(reply, "error")) {
            const gchar *error_msg = gst_structure_get_string(reply, "error");
            sf::error(pipeline_category, "create-answer failed: %s", error_msg ? error_msg : "unknown error");
            gst_promise_unref(promise);
            return;
        }

        GstSDPMessage *sdp_msg = nullptr;
        if (!gst_structure_get(reply, "sdp", GST_TYPE_SDP_MESSAGE, &sdp_msg, nullptr)) {
            sf::error(pipeline_category, "Failed to get SDP message from create-answer reply");
            gst_promise_unref(promise);
            return;
        }

        if (!sdp_msg) {
            sf::error(pipeline_category, "SDP message is null in create-answer reply");
            gst_promise_unref(promise);
            return;
        }

        sf::info(pipeline_category, "SDP answer created successfully");
        // Convert SDP message to string
        gchar *sdp_string = gst_sdp_message_as_text(sdp_msg);
        gst_sdp_message_free(sdp_msg);

        if (!sdp_string) {
            sf::error(pipeline_category, "Failed to convert SDP message to string");
            gst_promise_unref(promise);
            return;
        }

        const size_t sdp_length = strlen(sdp_string);
        sf::info(pipeline_category, "SDP answer string length: %zu bytes", sdp_length);
        sf::debug(pipeline_category, "SDP answer content:\n%s", sdp_string);

        // Send SDP answer via signaling
        const sf::communication::signaling_message msg{sf::communication::signaling_message_type::sdp_answer,
                                                       std::string(sdp_string)};

        if (data->signaling_server &&
            sf::communication::signaling_server_send_to_client(data->signaling_server, data->client_id, msg)) {
            sf::info(pipeline_category, "Successfully sent SDP answer via signaling to client %s",
                     data->client_id.c_str());
        } else {
            sf::error(pipeline_category, "Failed to send SDP answer via signaling to client %s",
                      data->client_id.c_str());
        }

        g_free(sdp_string);
        gst_promise_unref(promise);
    }

    /**
     * @brief Starts SDP answer creation when negotiation is required.
     * @param webrtcbin WebRTC bin requesting negotiation.
     * @param user_data Pointer to webrtc_signal_data for this client.
     */
    void on_webrtc_negotiation_needed(GstElement *webrtcbin, gpointer user_data) {
        auto *data = static_cast<webrtc_signal_data *>(user_data);
        (void) webrtcbin; // Unused - required by signal signature

        if (!data->signaling_server) {
            sf::error(pipeline_category, "Signaling server is null, cannot create SDP answer");
            return;
        }

        GstPromise *promise = gst_promise_new_with_change_func(handle_create_answer_reply, user_data, nullptr);

        if (!promise) {
            sf::error(pipeline_category, "Failed to create promise for create-answer");
            return;
        }

        sf::info(pipeline_category, "Triggering create-answer for webrtcbin...");
        g_signal_emit_by_name(webrtcbin, "create-answer", nullptr, promise);
    }

    /**
     * @brief Callback for webrtcbin "on-ice-candidate" signal.
     * Sends ICE candidate via signaling.
     */
    void on_webrtc_ice_candidate(GstElement *webrtcbin, const guint mline_index, gchar *candidate, gpointer user_data) {
        auto *data = static_cast<webrtc_signal_data *>(user_data);
        (void) webrtcbin; // Unused - required by signal signature
        (void) mline_index; // Unused - required by signal signature

        if (!candidate) {
            sf::warning(pipeline_category, "on_webrtc_ice_candidate called with null candidate");
            return;
        }

        sf::debug(pipeline_category, "ICE candidate generated: mline_index=%u, candidate=%s", mline_index, candidate);

        if (!data->signaling_server) {
            sf::error(pipeline_category, "Signaling server is null, cannot send ICE candidate");
            return;
        }

        // Send ICE candidate via signaling
        const sf::communication::signaling_message msg{sf::communication::signaling_message_type::ice_candidate,
                                                       std::string(candidate)};

        if (sf::communication::signaling_server_send_to_client(data->signaling_server, data->client_id, msg)) {
            sf::debug(pipeline_category, "Successfully sent ICE candidate via signaling to client %s: %s",
                      data->client_id.c_str(), candidate);
        } else {
            sf::error(pipeline_category, "Failed to send ICE candidate via signaling to client %s: %s",
                      data->client_id.c_str(), candidate);
        }
    }
} // namespace

/**
 * @brief Creates input pipeline elements.
 * @param pipeline The pipeline.
 * @return true if successful, false otherwise.
 */
bool create_input_elements(rtp_pipeline &pipeline) {
    // Delegate to transport functions
    if (!pipeline.transport_ctx.functions.create_input_elements) {
        return false;
    }
    return pipeline.transport_ctx.functions.create_input_elements(&pipeline.transport_build_ctx);
}

/**
 * @brief Creates processing pipeline elements.
 * @param pipeline The pipeline.
 * @return true if successful, false otherwise.
 */
bool create_processing_elements(rtp_pipeline &pipeline) {
    pipeline.queue_encoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "queue-encoded"));
    pipeline.queue_decoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "queue-decoded"));
    pipeline.sf_h265_sei_extract =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("sfh265seiextract", "sf-sei-extract"));
    try {
        const std::string decoder_factory_name = pipeline.server_config != nullptr
                                                         ? resolve_decoder_element_factory_name(*pipeline.server_config)
                                                         : std::string(get_default_decoder_element_name());
        // RAII_EXEMPT: GstBin takes ownership of these raw GstElement pointers.
        pipeline.h265_decoder =
                sf::unique_gst_element_no_delete_ptr(create_h265_decoder("decoder", decoder_factory_name).release());
        // RAII_EXEMPT: GstBin takes ownership of these raw GstElement pointers.
        pipeline.decoded_converter = sf::unique_gst_element_no_delete_ptr(
                create_decoded_postprocess_convert("decoded-convert", decoder_factory_name).release());
    } catch (const std::exception &error) {
        sf::error(pipeline_category, "Failed to create decoder/converter for port %u: %s", pipeline.get_port(),
                  error.what());
        return false;
    }
    pipeline.decoded_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "decoded-capsfilter"));
    pipeline.sf_pri_reconstruct =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("sfprireconstruct", "sf-pri-reconstruct"));
    const bool enable_luma_backscale =
            pipeline.server_config == nullptr || pipeline.server_config->enable_luma_backscale;
    if (enable_luma_backscale) {
        pipeline.sf_eoi_luma_backscale = sf::unique_gst_element_no_delete_ptr(
                gst_element_factory_make("sfeoilumabackscale", "sf-eoi-luma-backscale"));
    }
    pipeline.sf_tet_apply =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("sftetapply", "sf-tet-apply"));
    pipeline.decoded_output_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "decoded-output-capsfilter"));

    if (!pipeline.queue_encoded || !pipeline.queue_decoded || !pipeline.sf_h265_sei_extract || !pipeline.h265_decoder ||
        !pipeline.decoded_converter || !pipeline.decoded_capsfilter || !pipeline.sf_pri_reconstruct ||
        (enable_luma_backscale && !pipeline.sf_eoi_luma_backscale) || !pipeline.sf_tet_apply ||
        !pipeline.decoded_output_capsfilter) {
        return false;
    }
    return true;
}

/**
 * @brief Validates that all required elements were created successfully.
 * @param pipeline The pipeline.
 * @return true if all elements are valid, false otherwise.
 */
namespace {
    /**
     * @brief Validates a single element with error reporting.
     * @param element Element to validate.
     * @param element_name Name of the element for error messages.
     * @param port Port number for error messages.
     * @return true if element is valid, false otherwise.
     */
    [[nodiscard]] bool validate_element(GstElement *element, const char *element_name, guint16 port) {
        if (!element) {
            sf::error(pipeline_category, "Failed to create %s for port %u", element_name, port);
            return false;
        }
        return true;
    }

    /**
     * @brief Validates transport-specific input elements.
     * @param transport_type Transport type.
     * @param webrtcbin WebRTC bin element (for WebRTC transport).
     * @param webrtc_rtp_capsfilter WebRTC RTP capsfilter element.
     * @param rtp_input_source RTP input source element (for RTP input transport).
     * @param rtp_input_capsfilter Capsfilter element (for RTP input transport).
     * @param port Port number for error messages.
     * @return true if all elements are valid, false otherwise.
     */
    [[nodiscard]] bool validate_transport_elements(transport_type transport_type, GstElement *webrtcbin,
                                                   GstElement *webrtc_rtp_capsfilter, GstElement *rtp_input_source,
                                                   GstElement *rtp_input_capsfilter, guint16 port) {
        if (transport_type == transport_type::webrtc) {
            return validate_element(webrtcbin, "webrtcbin", port) &&
                   validate_element(webrtc_rtp_capsfilter, "webrtc_rtp_capsfilter", port);
        } else {
            return validate_element(rtp_input_source, "udpsrc", port) &&
                   validate_element(rtp_input_capsfilter, "capsfilter", port);
        }
    }

    /**
     * @brief Validates core pipeline elements (shared transport tail + encoded queue).
     */
    [[nodiscard]] bool validate_core_elements(GstElement *rtph265depay, GstElement *h265parse,
                                              GstElement *queue_encoded, guint16 port) {
        return validate_element(rtph265depay, "rtph265depay", port) && validate_element(h265parse, "h265parse", port) &&
               validate_element(queue_encoded, "queue_encoded", port);
    }

    /**
     * @brief Validates decoding pipeline elements (VCAI chain through output capsfilter).
     */
    [[nodiscard]] bool validate_decoding_elements(GstElement *sf_h265_sei_extract, GstElement *h265_decoder,
                                                  GstElement *decoded_converter, GstElement *decoded_capsfilter,
                                                  GstElement *sf_pri_reconstruct, GstElement *sf_eoi_luma_backscale,
                                                  bool enable_luma_backscale, GstElement *sf_tet_apply,
                                                  GstElement *queue_decoded, GstElement *decoded_output_capsfilter,
                                                  GstElement *gpu_video_convert, guint16 port) {
        return validate_element(sf_h265_sei_extract, "sf_h265_sei_extract", port) &&
               validate_element(h265_decoder, "h265 decoder", port) &&
               validate_element(decoded_converter, "decoded_converter", port) &&
               validate_element(decoded_capsfilter, "decoded_capsfilter", port) &&
               validate_element(sf_pri_reconstruct, "sf_pri_reconstruct", port) &&
               (!enable_luma_backscale || validate_element(sf_eoi_luma_backscale, "sf_eoi_luma_backscale", port)) &&
               validate_element(sf_tet_apply, "sf_tet_apply", port) &&
               validate_element(queue_decoded, "queue_decoded", port) &&
               validate_element(decoded_output_capsfilter, "decoded_output_capsfilter", port) &&
               validate_element(gpu_video_convert, "gpu_video_convert", port);
    }

    /**
     * @brief Validates output pipeline elements.
     * @param tee Tee element.
     * @param queue_display Display queue element.
     * @param output_sink Output sink element.
     * @param port Port number for error messages.
     * @return true if all elements are valid, false otherwise.
     */
    [[nodiscard]] bool validate_output_elements(GstElement *tee, GstElement *queue_display,
                                                GstElement *decoded_output_capsfilter, GstElement *output_sink,
                                                guint16 port) {
        return validate_element(tee, "tee", port) && validate_element(queue_display, "queue_display", port) &&
               validate_element(decoded_output_capsfilter, "decoded_output_capsfilter", port) &&
               validate_element(output_sink, "output_sink", port);
    }

    /**
     * @brief Validates file branch elements if file output is enabled.
     * @param output_directory Output directory (empty means file output disabled).
     * @param queue_file File queue element.
     * @param h265_encoder H.265 encoder element.
     * @param filesink File sink element.
     * @param port Port number for error messages.
     * @return true if all elements are valid or file output is disabled, false otherwise.
     */
    [[nodiscard]] bool validate_file_branch_elements(const std::optional<std::string> &output_directory,
                                                     GstElement *queue_file, GstElement *h265_encoder,
                                                     GstElement *filesink, guint16 port) {
        if (!output_directory.has_value() || output_directory->empty()) {
            return true; // File output not enabled
        }

        return validate_element(queue_file, "queue_file", port) &&
               validate_element(h265_encoder, "h265_encoder", port) && validate_element(filesink, "filesink", port);
    }
} // namespace

bool validate_elements(const rtp_pipeline &pipeline) {
    const guint16 port = pipeline.get_port();
    return validate_transport_elements(pipeline.transport_type_enum, pipeline.webrtcbin.get(),
                                       pipeline.webrtc_rtp_capsfilter.get(), pipeline.rtp_input_source.get(),
                                       pipeline.rtp_input_capsfilter.get(), port) &&
           validate_core_elements(pipeline.rtph265depay.get(), pipeline.h265parse.get(), pipeline.queue_encoded.get(),
                                  port) &&
           validate_decoding_elements(
                   pipeline.sf_h265_sei_extract.get(), pipeline.h265_decoder.get(), pipeline.decoded_converter.get(),
                   pipeline.decoded_capsfilter.get(), pipeline.sf_pri_reconstruct.get(),
                   pipeline.sf_eoi_luma_backscale.get(),
                   pipeline.server_config == nullptr || pipeline.server_config->enable_luma_backscale,
                   pipeline.sf_tet_apply.get(), pipeline.queue_decoded.get(), pipeline.decoded_output_capsfilter.get(),
                   pipeline.gpu_video_convert.get(), port) &&
           validate_output_elements(pipeline.tee.get(), pipeline.queue_display.get(),
                                    pipeline.decoded_output_capsfilter.get(), pipeline.output_sink.get(), port) &&
           validate_file_branch_elements(pipeline.output_directory, pipeline.queue_file.get(),
                                         pipeline.h265_encoder.get(), pipeline.filesink.get(), port);
}

bool create_elements(rtp_pipeline &pipeline) {
    if (!create_input_elements(pipeline)) {
        return false;
    }

    if (!create_processing_elements(pipeline)) {
        return false;
    }

    if (!pipeline.output_controller.initialize_enabled_branches(&pipeline)) {
        sf::error(pipeline_category, "Failed to initialize output branch controller for port %u", pipeline.get_port());
        return false;
    }

    if (!pipeline.output_controller.create_branch_elements(pipeline)) {
        sf::error(pipeline_category, "Failed to create output branch elements for port %u", pipeline.get_port());
        return false;
    }

    return validate_elements(pipeline);
}

bool configure_elements(rtp_pipeline &pipeline) {
    // Configure input elements via transport functions
    if (pipeline.transport_ctx.functions.configure_input_elements) {
        if (!pipeline.transport_ctx.functions.configure_input_elements(&pipeline.transport_build_ctx)) {
            sf::error(pipeline_category, "Failed to configure transport input elements for port %u", pipeline.port);
            return false;
        }
    }

    // Transport-specific configuration is handled by transport functions above

    // Configure capsfilter after h265parse to force byte-stream format
    // This ensures h265parse outputs byte-stream format with start codes for the decoder
    const sf::unique_gst_caps_ptr h265_caps(
            gst_caps_from_string("video/x-h265, stream-format=byte-stream, alignment=au"));
    g_object_set(G_OBJECT(pipeline.h265_capsfilter.get()), "caps", h265_caps.get(), nullptr);

    g_object_set(G_OBJECT(pipeline.queue_encoded.get()), "max-size-buffers", 5, "max-size-time", 0, "max-size-bytes", 0,
                 "leaky", 0, nullptr);
    g_object_set(G_OBJECT(pipeline.queue_decoded.get()), "max-size-buffers", 5, "max-size-time", 0, "max-size-bytes", 0,
                 "leaky", 0, nullptr);

    // Leave decoded_capsfilter unfixed so negotiated decoded metadata (for example
    // colorimetry and aspect ratio) can propagate through the decode chain.

    g_object_set(G_OBJECT(pipeline.sf_pri_reconstruct.get()), "strict-parsing", FALSE, "strict-reconstruction", FALSE,
                 "background-color", static_cast<guint>(pipeline.background_color), nullptr);
    {
        const std::filesystem::path roi_trace_path =
                get_debug_frames_root_directory(pipeline) / "tracker_dump" /
                ("rtp_pri_regions_port_" + std::to_string(static_cast<unsigned int>(pipeline.port)) + ".jsonl");
        std::error_code create_error;
        std::filesystem::create_directories(roi_trace_path.parent_path(), create_error);
        if (create_error) {
            sf::warning(pipeline_category, "Failed to create ROI trace directory '%s': %s",
                        roi_trace_path.parent_path().string().c_str(), create_error.message().c_str());
        } else {
            const std::string roi_trace_path_string = roi_trace_path.string();
            g_object_set(G_OBJECT(pipeline.sf_pri_reconstruct.get()), "roi-trace-path", roi_trace_path_string.c_str(),
                         nullptr);
            sf::info(pipeline_category, "PRI reconstruction ROI trace enabled: %s", roi_trace_path_string.c_str());
        }
    }
    if (pipeline.sf_eoi_luma_backscale) {
        g_object_set(G_OBJECT(pipeline.sf_eoi_luma_backscale.get()), "enabled", TRUE, "strict-parsing", FALSE, nullptr);
    }
    if (pipeline.server_config != nullptr) {
        const gboolean tet_enabled = pipeline.server_config->tet_enabled ? TRUE : FALSE;
        const guint tet_qp = static_cast<guint>(pipeline.server_config->tet_qp);
        const gboolean dynamic_qp = pipeline.server_config->tet_dynamic_qp ? TRUE : FALSE;
        const gchar *tile_file =
                pipeline.server_config->tet_tile_file.empty() ? "" : pipeline.server_config->tet_tile_file.c_str();
        g_object_set(G_OBJECT(pipeline.sf_tet_apply.get()), "enabled", tet_enabled, "tile-file", tile_file, "qp",
                     tet_qp, "dynamic-qp", dynamic_qp, "strict-apply", FALSE, nullptr);
    } else {
        g_object_set(G_OBJECT(pipeline.sf_tet_apply.get()), "enabled", FALSE, "strict-apply", FALSE, nullptr);
    }

    // Leave decoded_output_capsfilter unfixed so negotiated raw-video metadata can propagate downstream.

    if (!pipeline.output_controller.configure_branch_elements(pipeline)) {
        sf::error(pipeline_category, "Failed to configure output branches for port %u", pipeline.get_port());
        return false;
    }

    return true;
}

bool add_elements_to_pipelines(rtp_pipeline &pipeline) {
    GstElement *const main = pipeline.main_pipeline.get();
    if (!main) {
        sf::error(pipeline_category, "Main pipeline is null for port %u", pipeline.get_port());
        return false;
    }

    if (pipeline.transport_type_enum == transport_type::webrtc) {
        // Note: webrtcbin -> webrtc_rtp_capsfilter is linked dynamically in on_webrtc_pad_added
        gst_bin_add_many(GST_BIN(main), pipeline.webrtcbin.get(), pipeline.webrtc_rtp_capsfilter.get(),
                         pipeline.rtph265depay.get(), pipeline.h265parse.get(), pipeline.h265_capsfilter.get(),
                         pipeline.queue_encoded.get(), nullptr);
    } else {
        gst_bin_add_many(GST_BIN(main), pipeline.rtp_input_source.get(), pipeline.rtp_input_capsfilter.get(),
                         pipeline.rtph265depay.get(), pipeline.h265parse.get(), pipeline.h265_capsfilter.get(),
                         pipeline.queue_encoded.get(), nullptr);
    }

    gst_bin_add_many(GST_BIN(main), pipeline.sf_h265_sei_extract.get(), pipeline.h265_decoder.get(),
                     pipeline.decoded_converter.get(), pipeline.decoded_capsfilter.get(),
                     pipeline.sf_pri_reconstruct.get(), pipeline.sf_tet_apply.get(), pipeline.queue_decoded.get(),
                     pipeline.decoded_output_capsfilter.get(), nullptr);
    if (pipeline.sf_eoi_luma_backscale) {
        gst_bin_add(GST_BIN(main), pipeline.sf_eoi_luma_backscale.get());
    }

    if (!pipeline.output_controller.add_branches_to_pipeline(pipeline, main)) {
        sf::error(pipeline_category, "Failed to add output branches to pipeline for port %u", pipeline.get_port());
        return false;
    }

    return true;
}

bool setup_webrtcbin_signals(transport_build_context &context, sf::communication::signaling_server *signaling_server,
                             const std::string &client_id) {
    if (!context.webrtcbin || context.transport_type_enum != transport_type::webrtc) {
        return true; // Not WebRTC, nothing to do
    }

    if (!signaling_server) {
        sf::error(pipeline_category, "Signaling server not provided for WebRTC pipeline");
        return false;
    }

    // Allocate signal data structure and store it with pipeline ownership.
    context.runtime_context.webrtc_signal_data_ptr =
            std::make_unique<webrtc_signal_data>(webrtc_signal_data{&context, signaling_server, client_id});
    auto *signal_data_ptr = context.runtime_context.webrtc_signal_data_ptr.get();

    // Connect webrtcbin signals
    const gulong negotiation_id = g_signal_connect(context.webrtcbin.get(), "on-negotiation-needed",
                                                   G_CALLBACK(on_webrtc_negotiation_needed), signal_data_ptr);
    context.runtime_context.signal_handler_infos.push_back({context.webrtcbin.get(), negotiation_id});

    const gulong ice_candidate_id = g_signal_connect(context.webrtcbin.get(), "on-ice-candidate",
                                                     G_CALLBACK(on_webrtc_ice_candidate), signal_data_ptr);
    context.runtime_context.signal_handler_infos.push_back({context.webrtcbin.get(), ice_candidate_id});

    // Connect pad-added signal for dynamic pad linking
    const gulong pad_added_id =
            g_signal_connect(context.webrtcbin.get(), "pad-added", G_CALLBACK(on_webrtc_pad_added), signal_data_ptr);
    context.runtime_context.signal_handler_infos.push_back({context.webrtcbin.get(), pad_added_id});

    sf::info(pipeline_category, "WebRTC signals configured for port %u (client: %s)", context.port, client_id.c_str());

    return true;
}
