// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "webrtc_transport.h"
#include "../../core/server.h"
#include "../../pipeline/context/pipeline_contexts.h"
#include "../../pipeline/pipeline/creation.h"
#include "../../pipeline/pipeline/linking.h"
#include "logging.h"
#include "webrtc_signaling.h"

#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <memory>

// Debug category for WebRTC transport operations.
GST_DEBUG_CATEGORY_STATIC(webrtc_transport_category);
// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);
#define GST_CAT_DEFAULT webrtc_transport_category

transport_functions create_webrtc_transport_functions(const rtp_server_config *config) {
    // Capture config in lambda closures
    const rtp_server_config *captured_config = config;

    return {.initialize_server = [captured_config](rtp_server *server) -> bool {
                if (!captured_config || !server) {
                    return false;
                }

                // Initialize debug category if not already initialized
                GST_DEBUG_CATEGORY_INIT(webrtc_transport_category, "webrtc-transport", 0, "WebRTC Transport");

                const uint16_t signaling_port = captured_config->webrtc_signaling_port;
                const bool use_ssl = captured_config->webrtc_signaling_use_ssl;

                // Create signaling server callbacks
                const sf::communication::signaling_client_connected_callback client_connected =
                        [server](const std::string &client_id) { server->handle_webrtc_client_connected(client_id); };

                const sf::communication::signaling_client_disconnected_callback client_disconnected =
                        [server](const std::string &client_id) {
                            server->handle_webrtc_client_disconnected(client_id);
                        };

                const sf::communication::signaling_server_message_callback message_callback =
                        [server](const std::string &client_id, const sf::communication::signaling_message &msg) {
                            server->handle_webrtc_signaling_message_callback(client_id, msg);
                        };

                auto signaling_server = sf::communication::create_signaling_server(
                        signaling_port, use_ssl, client_connected, client_disconnected, message_callback);
                if (!signaling_server) {
                    sf::error(webrtc_transport_category, "Failed to create signaling server");
                    return false;
                }

                if (!sf::communication::signaling_server_start(signaling_server.get())) {
                    sf::error(webrtc_transport_category, "Failed to start signaling server");
                    return false;
                }

                // Store signaling server in server
                server->set_signaling_server(std::move(signaling_server));

                sf::info(webrtc_transport_category, "WebRTC signaling server started on port %u (SSL: %s)",
                         signaling_port, use_ssl ? "yes" : "no");

                return true;
            },

            .create_input_elements = [](transport_build_context *context) -> bool {
                if (!context) {
                    return false;
                }

                // WebRTC: webrtcbin -> webrtc_rtp_capsfilter -> rtph265depay -> h265parse -> h265_capsfilter ->
                // queue_encoded
                context->webrtcbin =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("webrtcbin", "webrtcbin"));
                context->webrtc_rtp_capsfilter =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "webrtc-rtp-caps"));
                context->rtph265depay =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("rtph265depay", nullptr));
                context->h265parse =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("h265parse", nullptr));
                context->h265_capsfilter = sf::unique_gst_element_no_delete_ptr(
                        gst_element_factory_make("capsfilter", "h265-byte-stream"));

                if (!context->webrtcbin || !context->webrtc_rtp_capsfilter || !context->rtph265depay ||
                    !context->h265parse || !context->h265_capsfilter) {
                    return false;
                }

                return true;
            },

            .configure_input_elements = [](transport_build_context *context) -> bool {
                if (!context) {
                    return false;
                }

                // Configure webrtc_rtp_capsfilter with H.265 RTP caps including clock-rate
                const sf::unique_gst_caps_ptr rtp_caps(
                        gst_caps_from_string("application/x-rtp, media=video, clock-rate=90000, encoding-name=H265"));
                if (rtp_caps) {
                    g_object_set(G_OBJECT(context->webrtc_rtp_capsfilter.get()), "caps", rtp_caps.get(), nullptr);
                    sf::info(pipeline_category,
                             "WebRTC RTP capsfilter configured with H.265 caps (clock-rate=90000) for port %u",
                             context->port);
                } else {
                    sf::warning(pipeline_category, "Failed to create H.265 RTP caps for port %u", context->port);
                    return false;
                }

                return true;
            },

            .link_input_pipeline = [](transport_build_context *context) -> bool {
                if (!context) {
                    return false;
                }

                // WebRTC: webrtcbin pads are created dynamically, so we can't link webrtcbin directly
                // The pad-added signal handler will link webrtcbin's src pad to webrtc_rtp_capsfilter when it becomes
                // available For now, link the rest: webrtc_rtp_capsfilter -> rtph265depay -> h265parse ->
                // h265_capsfilter -> queue_encoded
                if (!gst_element_link_many(context->webrtc_rtp_capsfilter.get(), context->rtph265depay.get(),
                                           context->h265parse.get(), context->h265_capsfilter.get(),
                                           context->queue_encoded.get(), nullptr)) {
                    sf::error(
                            pipeline_category,
                            "Failed to link WebRTC input pipeline (webrtc_rtp_capsfilter -> queue_encoded) for port %u",
                            context->port);
                    return false;
                }

                return true;
            },

            .setup_signals = [](transport_build_context *context, sf::communication::signaling_server *signaling_server,
                                const std::string *client_id) -> bool {
                if (!context || !signaling_server || !client_id) {
                    return false;
                }

                // Use existing setup_webrtcbin_signals function
                return setup_webrtcbin_signals(*context, signaling_server, *client_id);
            },

            .handle_signaling_message =
                    [](transport_build_context *context, const sf::communication::signaling_message *msg,
                       const std::string *client_id, sf::communication::signaling_server *signaling_server) -> void {
                if (!context || !msg || !client_id || !signaling_server) {
                    return;
                }

                // Use existing handle_webrtc_signaling_message function (declared as friend in server.h)
                handle_webrtc_signaling_message(context, *msg, *client_id, signaling_server);
            },

            .cleanup = [](transport_build_context *context) -> void {
                // Cleanup WebRTC-specific resources
                (void) context;
            },

            .get_transport_type = []() -> transport_type { return transport_type::webrtc; }};
}

transport_context create_webrtc_transport_context(const rtp_server_config *config) {
    if (!config) {
        return {.functions = {}};
    }

    return {.functions = create_webrtc_transport_functions(config)};
}
