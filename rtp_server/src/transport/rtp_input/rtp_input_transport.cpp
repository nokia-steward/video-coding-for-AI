// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rtp_input_transport.h"
#include "../../core/server.h"
#include "../../pipeline/context/pipeline_contexts.h"
#include "logging.h"

#include <gst/gst.h>

// Debug category for RTP input transport operations.
GST_DEBUG_CATEGORY_STATIC(rtp_input_transport_category);
// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);
#define GST_CAT_DEFAULT rtp_input_transport_category

namespace {
    /**
     * @brief Create RTP input handshake socket (RTP input server initialization).
     */
    [[nodiscard]] bool create_rtp_input_handshake_socket(rtp_server *server) {
        if (!server) {
            return false;
        }
        return server->create_handshake_socket();
    }

    /**
     * @brief Setup RTP input socket watcher (RTP input server initialization).
     */
    void setup_rtp_input_socket_watcher(rtp_server *server) {
        if (!server) {
            return;
        }
        server->setup_socket_watcher();
    }
} // namespace

transport_functions create_rtp_input_transport_functions() {
    return {.initialize_server = [](rtp_server *server) -> bool {
                if (!server) {
                    return false;
                }

                // Initialize debug category if not already initialized
                GST_DEBUG_CATEGORY_INIT(rtp_input_transport_category, "rtp-input-transport", 0, "RTP Input Transport");

                // Create RTP input handshake socket
                if (!create_rtp_input_handshake_socket(server)) {
                    sf::error(rtp_input_transport_category, "Failed to create RTP input handshake socket");
                    return false;
                }

                // Setup socket watcher
                setup_rtp_input_socket_watcher(server);

                return true;
            },

            .create_input_elements = [](transport_build_context *context) -> bool {
                if (!context) {
                    return false;
                }

                // RTP input: udpsrc -> capsfilter -> rtph265depay -> h265parse -> h265_capsfilter -> queue_encoded
                context->rtp_input_source =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("udpsrc", nullptr));
                context->rtp_input_capsfilter =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", nullptr));
                context->rtph265depay =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("rtph265depay", nullptr));
                context->h265parse =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("h265parse", nullptr));
                context->h265_capsfilter = sf::unique_gst_element_no_delete_ptr(
                        gst_element_factory_make("capsfilter", "h265-byte-stream"));

                if (!context->rtp_input_source || !context->rtp_input_capsfilter || !context->rtph265depay ||
                    !context->h265parse || !context->h265_capsfilter) {
                    return false;
                }

                return true;
            },

            .configure_input_elements = [](transport_build_context *context) -> bool {
                if (!context) {
                    return false;
                }

                // Configure RTP input source with large buffer to prevent packet loss on first I-frame
                // The first frame is typically a large I-frame that gets fragmented into many packets.
                // Without sufficient buffer space, packets can be dropped before the pipeline starts reading.
                constexpr gint rtp_input_buffer_size = 2097152; // 2MB buffer
                g_object_set(G_OBJECT(context->rtp_input_source.get()), "port", static_cast<gint>(context->port),
                             "buffer-size", rtp_input_buffer_size, nullptr);
                sf::info(rtp_input_transport_category, "Configured RTP input source for port %u with buffer-size=%d",
                         context->port, rtp_input_buffer_size);

                // Configure capsfilter for RTP H.265
                const sf::unique_gst_caps_ptr caps(
                        gst_caps_from_string("application/x-rtp, media=video, clock-rate=90000, encoding-name=H265"));
                g_object_set(G_OBJECT(context->rtp_input_capsfilter.get()), "caps", caps.get(), nullptr);

                return true;
            },

            .link_input_pipeline = [](transport_build_context *context) -> bool {
                if (!context) {
                    return false;
                }

                // RTP input: udpsrc -> capsfilter -> rtph265depay -> h265parse -> h265_capsfilter -> queue_encoded
                // The h265_capsfilter forces byte-stream format output from h265parse
                if (!gst_element_link_many(context->rtp_input_source.get(), context->rtp_input_capsfilter.get(),
                                           context->rtph265depay.get(), context->h265parse.get(),
                                           context->h265_capsfilter.get(), context->queue_encoded.get(), nullptr)) {
                    sf::error(pipeline_category, "Failed to link RTP input pipeline for port %u", context->port);
                    return false;
                }

                return true;
            },

            .setup_signals = [](transport_build_context *context, sf::communication::signaling_server *signaling_server,
                                const std::string *client_id) -> bool {
                // No-op: RTP input doesn't need signal handlers
                (void) context;
                (void) signaling_server;
                (void) client_id;
                return true;
            },

            .handle_signaling_message =
                    [](transport_build_context *context, const sf::communication::signaling_message *msg,
                       const std::string *client_id, sf::communication::signaling_server *signaling_server) -> void {
                // No-op: RTP input doesn't use signaling
                (void) context;
                (void) msg;
                (void) client_id;
                (void) signaling_server;
            },

            .cleanup = [](transport_build_context *context) -> void {
                // Cleanup RTP input-specific resources if any
                (void) context;
            },

            .get_transport_type = []() -> transport_type { return transport_type::rtp; }};
}

transport_context create_rtp_input_transport_context() { return {.functions = create_rtp_input_transport_functions()}; }
