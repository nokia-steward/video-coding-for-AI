// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "webrtc_branch.h"
#include "../../../../../libvcai/include/communication/signaling.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../../encoder_enums.h"
#include "../../../../global_data.h"
#include "../../../pipeline_utils.h"
#include "../../../rtp_utils.h"
#include "../../elements.h"
#include "../../output_pipeline_debug.h"
#include "../output_branch_facade.h"

#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <optional>

/**
 * @file webrtc_branch.cpp
 * @brief Implementation of WebRTC branch creation and linking.
 */

// WebRTC headers - try to include if available
#ifdef HAVE_GSTREAMER_WEBRTC
#include <gst/webrtc/webrtc.h>
#else
// Forward declaration for GstWebRTCSessionDescription when headers aren't available
typedef struct _GstWebRTCSessionDescription GstWebRTCSessionDescription;
struct _GstWebRTCSessionDescription {
    int type; // GstWebRTCSDPType
    GstSDPMessage *sdp;
};
/**
 * @brief Looks up the boxed GType for GstWebRTCSessionDescription.
 * @return GType for WebRTC session description.
 */
inline GType get_webrtc_session_description_type() {
    static GType type = G_TYPE_INVALID;
    if (type == G_TYPE_INVALID) {
        type = g_type_from_name("GstWebRTCSessionDescription");
    }
    return type;
}
#define GST_TYPE_WEBRTC_SESSION_DESCRIPTION (get_webrtc_session_description_type())
#endif

namespace {
    /**
     * @brief Tests if RTP plugins are available.
     * @return true if available, false otherwise.
     */
    [[nodiscard]] bool test_rtp_plugins_available() {
        GstElement *test_rtp_payloader = gst_element_factory_make("rtph265pay", "test-rtp-pay");
        GstElement *test_udp_sink = gst_element_factory_make("udpsink", "test-udp-sink");

        const bool available = test_rtp_payloader && test_udp_sink;

        if (test_rtp_payloader) {
            gst_object_unref(test_rtp_payloader);
        }
        if (test_udp_sink) {
            gst_object_unref(test_udp_sink);
        }

        return available;
    }

    /**
     * @brief Tests if webrtcbin plugin is available.
     * @return true if available, false otherwise.
     */
    [[nodiscard]] bool test_webrtcbin_available() {
        GstElement *test_webrtcbin = gst_element_factory_make("webrtcbin", "test-webrtcbin");
        const bool available = test_webrtcbin != nullptr;

        if (test_webrtcbin) {
            gst_object_unref(test_webrtcbin);
        }

        return available;
    }

    // Global signaling client for WebRTC (set during setup_signals)
    std::unique_ptr<sf::communication::signaling_client, void (*)(sf::communication::signaling_client *)>
            g_signaling_client(nullptr, sf::communication::destroy_signaling_client);

    /**
     * @brief Helper function to create offer SDP description for set-local-description.
     */
    // RAII_EXEMPT: GStreamer WebRTC API expects raw GstWebRTCSessionDescription* for signal interop.
    [[nodiscard]] GstWebRTCSessionDescription *create_offer_description_copy(GstSDPMessage *sdp_msg) {
        if (!sdp_msg) {
            return nullptr;
        }

        GstSDPMessage *sdp_copy = nullptr;
        if (gst_sdp_message_copy(sdp_msg, &sdp_copy) != GST_SDP_OK || !sdp_copy) {
            return nullptr;
        }

        constexpr int WEBRTC_SDP_TYPE_OFFER = 1;
        auto *offer_desc_copy =
                static_cast<GstWebRTCSessionDescription *>(g_malloc0(sizeof(GstWebRTCSessionDescription)));
        if (!offer_desc_copy) {
            gst_sdp_message_free(sdp_copy);
            return nullptr;
        }

        offer_desc_copy->type = WEBRTC_SDP_TYPE_OFFER;
        offer_desc_copy->sdp = sdp_copy;
        return offer_desc_copy;
    }

    /**
     * @brief Extracts offer from create-offer promise reply.
     */
    [[nodiscard]] const GstWebRTCSessionDescription *extract_offer_from_reply(const GstStructure *reply) {
        if (!reply) {
            sf::error(output_pipeline, "create-offer promise reply is null");
            return nullptr;
        }

        if (gst_structure_has_name(reply, "error")) {
            const gchar *error_msg = gst_structure_get_string(reply, "error");
            sf::error(output_pipeline, "create-offer failed: %s", error_msg ? error_msg : "unknown error");
            return nullptr;
        }

        const GType offer_type = GST_TYPE_WEBRTC_SESSION_DESCRIPTION;
        if (offer_type == G_TYPE_INVALID) {
            sf::error(output_pipeline, "GstWebRTCSessionDescription type not registered");
            return nullptr;
        }
        const GValue *offer_value = gst_structure_get_value(reply, "offer");
        if (!offer_value) {
            sf::error(output_pipeline, "create-offer reply does not contain 'offer' field");
            return nullptr;
        }
        const auto *offer_desc = static_cast<GstWebRTCSessionDescription *>(g_value_get_boxed(offer_value));
        if (!offer_desc || !offer_desc->sdp) {
            sf::error(output_pipeline, "create-offer returned invalid session description");
            return nullptr;
        }

        return offer_desc;
    }

    /**
     * @brief Copies SDP message for sending via signaling.
     */
    [[nodiscard]] GstSDPMessage *copy_sdp_message_for_signaling(const GstSDPMessage *sdp_msg) {
        GstSDPMessage *sdp_msg_copy = nullptr;
        if (gst_sdp_message_copy(sdp_msg, &sdp_msg_copy) != GST_SDP_OK || !sdp_msg_copy) {
            sf::error(output_pipeline, "Failed to copy SDP message");
            return nullptr;
        }
        return sdp_msg_copy;
    }

    /**
     * @brief Sets local description and sends SDP offer via signaling.
     */
    void set_local_description_and_send_offer(GstElement *webrtcbin_elem, const GstWebRTCSessionDescription *offer_desc,
                                              GstSDPMessage *sdp_msg_copy) {
        GstWebRTCSessionDescription *offer_for_local = create_offer_description_copy(offer_desc->sdp);
        if (!offer_for_local) {
            sf::error(output_pipeline, "Failed to create offer copy for set-local-description");
            gst_sdp_message_free(sdp_msg_copy);
            return;
        }

        g_signal_emit_by_name(webrtcbin_elem, "set-local-description", offer_for_local, nullptr);

        gchar *sdp_string = gst_sdp_message_as_text(sdp_msg_copy);
        if (sdp_string) {
            const sf::communication::signaling_message msg{sf::communication::signaling_message_type::sdp_offer,
                                                           std::string(sdp_string)};

            if (g_signaling_client && sf::communication::signaling_client_is_connected(g_signaling_client.get())) {
                const bool sent = sf::communication::signaling_client_send(g_signaling_client.get(), msg);
                if (sent) {
                    sf::info(output_pipeline, "Sent SDP offer via signaling");
                } else {
                    sf::error(output_pipeline, "Failed to send SDP offer via signaling");
                }
            } else {
                sf::error(output_pipeline, "Signaling client not connected, cannot send SDP offer");
            }

            g_free(sdp_string);
        } else {
            sf::error(output_pipeline, "Failed to convert SDP message to string");
        }

        gst_sdp_message_free(sdp_msg_copy);
    }

    /**
     * @brief Handles the create-offer promise callback.
     */
    void handle_create_offer_promise(GstPromise *promise_local, gpointer user_data_local) {
        auto *webrtcbin_elem = static_cast<GstElement *>(user_data_local);

        const GstStructure *reply = gst_promise_get_reply(promise_local);
        const GstWebRTCSessionDescription *offer_desc = extract_offer_from_reply(reply);
        if (!offer_desc) {
            gst_object_unref(webrtcbin_elem);
            gst_promise_unref(promise_local);
            return;
        }

        GstSDPMessage *sdp_msg_copy = copy_sdp_message_for_signaling(offer_desc->sdp);
        if (!sdp_msg_copy) {
            gst_object_unref(webrtcbin_elem);
            gst_promise_unref(promise_local);
            return;
        }

        set_local_description_and_send_offer(webrtcbin_elem, offer_desc, sdp_msg_copy);

        gst_object_unref(webrtcbin_elem);
        gst_promise_unref(promise_local);
    }

    /**
     * @brief Callback for webrtcbin "on-negotiation-needed" signal.
     */
    void on_negotiation_needed(GstElement *webrtcbin, [[maybe_unused]] gpointer user_data) {
        sf::debug(output_pipeline, "on-negotiation-needed signal triggered");

        if (!g_signaling_client) {
            sf::error(output_pipeline, "Signaling client is null in on_negotiation_needed callback");
            return;
        }

        if (!sf::communication::signaling_client_is_connected(g_signaling_client.get())) {
            sf::error(output_pipeline, "Signaling client not connected, cannot create SDP offer");
            return;
        }

        gst_object_ref(webrtcbin);

        GstPromise *promise = gst_promise_new_with_change_func(handle_create_offer_promise, webrtcbin, nullptr);

        if (!promise) {
            sf::error(output_pipeline, "Failed to create promise for create-offer");
            gst_object_unref(webrtcbin);
            return;
        }

        g_signal_emit_by_name(webrtcbin, "create-offer", nullptr, promise);
    }

    /**
     * @brief Handles the set-remote-description promise callback for SDP answer.
     */
    void handle_set_remote_description_answer_promise(GstPromise *promise_param, gpointer user_data_param) {
        (void) user_data_param;
        const GstStructure *reply = gst_promise_get_reply(promise_param);
        if (reply && gst_structure_has_name(reply, "error")) {
            const gchar *error_msg = gst_structure_get_string(reply, "error");
            sf::error(output_pipeline, "Failed to set remote description: %s", error_msg ? error_msg : "unknown error");
        }
        gst_promise_unref(promise_param);
    }

    /**
     * @brief Handles signaling messages received from the signaling server.
     */
    void handle_signaling_message(const output_pipeline_elements &elements,
                                  const sf::communication::signaling_message &msg) {
        if (msg.type == sf::communication::signaling_message_type::sdp_answer) {
            sf::info(output_pipeline, "Received SDP answer (%zu bytes)", msg.data.length());
            GstSDPMessage *sdp_msg = nullptr;
            if (gst_sdp_message_new(&sdp_msg) == GST_SDP_OK) {
                if (gst_sdp_message_parse_buffer(reinterpret_cast<const guint8 *>(msg.data.c_str()),
                                                 static_cast<guint>(msg.data.length()), sdp_msg) == GST_SDP_OK) {
                    constexpr int WEBRTC_SDP_TYPE_ANSWER = 3;

                    auto *answer_desc =
                            static_cast<GstWebRTCSessionDescription *>(g_malloc0(sizeof(GstWebRTCSessionDescription)));
                    answer_desc->type = WEBRTC_SDP_TYPE_ANSWER;
                    answer_desc->sdp = sdp_msg;

                    GstPromise *promise_local = gst_promise_new_with_change_func(
                            handle_set_remote_description_answer_promise, nullptr, nullptr);

                    g_signal_emit_by_name(elements.webrtcbin, "set-remote-description", answer_desc, promise_local);
                } else {
                    sf::error(output_pipeline, "Failed to parse SDP answer");
                    gst_sdp_message_free(sdp_msg);
                }
            } else {
                sf::error(output_pipeline, "Failed to create SDP message structure");
            }
        } else if (msg.type == sf::communication::signaling_message_type::ice_candidate) {
            g_signal_emit_by_name(elements.webrtcbin, "add-ice-candidate", 0, msg.data.c_str());
        }
    }

    /**
     * @brief Callback for webrtcbin "on-ice-candidate" signal.
     */
    void on_ice_candidate(GstElement *webrtcbin, const guint mline_index, gchar *candidate, gpointer user_data) {
        (void) webrtcbin;
        (void) mline_index;
        (void) user_data;

        if (!candidate) {
            sf::warning(output_pipeline, "on_ice_candidate called with null candidate");
            return;
        }

        sf::debug(output_pipeline, "ICE candidate generated: mline_index=%u, candidate=%s", mline_index, candidate);

        const sf::communication::signaling_message msg{sf::communication::signaling_message_type::ice_candidate,
                                                       std::string(candidate)};

        if (g_signaling_client && sf::communication::signaling_client_is_connected(g_signaling_client.get())) {
            const bool sent = sf::communication::signaling_client_send(g_signaling_client.get(), msg);
            if (sent) {
                sf::debug(output_pipeline, "Successfully sent ICE candidate via signaling: %s", candidate);
            } else {
                sf::error(output_pipeline, "Failed to send ICE candidate via signaling: %s", candidate);
            }
        } else {
            sf::error(output_pipeline, "Signaling client not connected, cannot send ICE candidate: %s", candidate);
        }
    }

    /**
     * @brief Callback for webrtcbin "notify::ice-connection-state" signal.
     */
    void on_ice_connection_state(GstElement *webrtcbin, GParamSpec *pspec, gpointer user_data) {
        (void) pspec;

        auto *data = static_cast<global_data *>(user_data);
        if (!data) {
            sf::error(output_pipeline, "on_ice_connection_state called with null user_data");
            return;
        }

        gint state_int = 0;
        g_object_get(webrtcbin, "ice-connection-state", &state_int, nullptr);

        constexpr gint ICE_CONNECTION_STATE_CONNECTED = 2;
        constexpr gint ICE_CONNECTION_STATE_COMPLETED = 3;
        constexpr gint ICE_CONNECTION_STATE_FAILED = 4;

        if (state_int == ICE_CONNECTION_STATE_CONNECTED || state_int == ICE_CONNECTION_STATE_COMPLETED) {
            std::lock_guard<std::mutex> lock(data->webrtc.connection_mutex);
            if (!data->webrtc.connection_ready.load()) {
                data->webrtc.connection_ready = true;
                data->webrtc.connection_cv.notify_all();
                sf::info(output_pipeline, "WebRTC ICE connection established (state=%d) - ready to send frames",
                         state_int);
            }
        } else if (state_int == ICE_CONNECTION_STATE_FAILED) {
            sf::error(output_pipeline, "WebRTC ICE connection failed");
        } else {
            sf::debug(output_pipeline, "WebRTC ICE connection state changed: %d", state_int);
        }
    }

    /**
     * @brief Check if WebRTC branch should be enabled.
     */
    [[nodiscard]] bool is_webrtc_branch_enabled(const output_branch_facade &data) {
        return !data.params.communication.host.empty() && data.params.communication.port > 0 &&
               data.params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc &&
               !data.params.webrtc.signaling_url.empty();
    }

    /**
     * @brief Performs communication handshake and creates WebRTC streaming elements.
     */
    [[nodiscard]] std::optional<rtp_handshake_result>
    perform_handshake_and_create_webrtc_elements(const output_branch_facade &data, output_pipeline_elements &elements) {
        const auto handshake = perform_rtp_handshake(data.params.communication.host, data.params.communication.port,
                                                     "webrtc", data.params.webrtc.signaling_url);
        if (!handshake) {
            sf::error(output_pipeline, "Communication handshake failed, disabling WebRTC streaming");
            sf::error(output_pipeline, "Please ensure the communication server is properly configured and responding");
            return std::nullopt;
        }

        // WebRTC: queue -> rtph265pay -> webrtcbin
        elements.queue_rtp = gst_element_factory_make("queue", "q-rtp");
        elements.rtp_payloader = gst_element_factory_make("rtph265pay", "rtp-pay");
        elements.webrtcbin = gst_element_factory_make("webrtcbin", "webrtcbin");

        if (!elements.queue_rtp || !elements.rtp_payloader || !elements.webrtcbin) {
            sf::error(output_pipeline, "Failed to create WebRTC elements");
            return std::nullopt;
        }

        return handshake;
    }

    /**
     * @brief Creates WebRTC branch elements.
     */
    [[nodiscard]] bool create_webrtc_branch_elements(output_branch_facade &data, output_pipeline_elements &elements) {
        if (!test_webrtcbin_available()) {
            sf::warning(output_pipeline, "webrtcbin plugin not available, disabling WebRTC streaming");
            return false;
        }
        if (!test_rtp_plugins_available()) {
            sf::warning(output_pipeline, "RTP plugins not available, disabling WebRTC streaming");
            return false;
        }

        const auto handshake = perform_handshake_and_create_webrtc_elements(data, elements);
        if (!handshake) {
            return false;
        }

        data.assigned_rtp_port.store(handshake->assigned_port);
        data.assigned_rtp_session_id.store(handshake->session_id);
        return true;
    }

    /**
     * @brief Configures WebRTC branch elements.
     */
    [[nodiscard]] bool configure_webrtc_branch_elements(const output_branch_facade &data,
                                                        output_pipeline_elements &elements) {
        // Configure WebRTC branch queue
        if (elements.queue_rtp) {
            // Uniform queue size for synchronization across all branches
            const int effective_framerate = data.params.framerate > 0 ? data.params.framerate : 60;
            const int max_buffers = 2 * effective_framerate;
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_rtp, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured WebRTC branch queue: max-size-buffers=%d, leaky=no_leak",
                      max_buffers);
        }

        // For WebRTC: Do NOT set "pt" property on rtph265pay
        if (elements.rtp_payloader) {
            g_object_set(elements.rtp_payloader, "config-interval", 1, nullptr);
        }

        // Configure webrtcbin
        if (elements.webrtcbin) {
            if (!data.params.webrtc.stun_server.empty()) {
                g_object_set(elements.webrtcbin, "stun-server", data.params.webrtc.stun_server.c_str(), nullptr);
            }
            g_object_set(elements.webrtcbin, "bundle-policy", 1, nullptr);
            sf::info(output_pipeline, "WebRTC streaming configured (signaling: %s)",
                     data.params.webrtc.signaling_url.c_str());
        }

        return true;
    }

    /**
     * @brief Links WebRTC branch elements.
     */
    [[nodiscard]] bool link_webrtc_branch(const output_pipeline_elements &elements) {
        if (!gst_element_link_many(elements.queue_rtp, elements.rtp_payloader, elements.webrtcbin, nullptr)) {
            sf::error(output_pipeline, "Failed to link WebRTC RTP branch");
            return false;
        }

        // Configure transceiver codec-preferences
        GArray *transceivers = nullptr;
        g_signal_emit_by_name(elements.webrtcbin, "get-transceivers", &transceivers);

        if (transceivers && transceivers->len > 0) {
            auto *transceiver = g_array_index(transceivers, gpointer, 0);
            if (transceiver) {
                GstCaps *codec_prefs = gst_caps_from_string(
                        "application/x-rtp, media=video, encoding-name=H265, clock-rate=90000, payload=96");

                if (codec_prefs) {
                    g_object_set(G_OBJECT(transceiver), "codec-preferences", codec_prefs, nullptr);
                    sf::info(output_pipeline, "Set WebRTC transceiver codec-preferences with pt=96");
                    gst_caps_unref(codec_prefs);
                } else {
                    sf::warning(output_pipeline, "Failed to create codec-preferences caps");
                }
            }
            g_array_unref(transceivers);
        } else {
            sf::warning(output_pipeline, "No transceivers found after linking to webrtcbin");
        }

        return true;
    }

    /**
     * @brief Links tee to WebRTC branch.
     */
    [[nodiscard]] bool link_tee_to_webrtc_branch(GstElement *tee, const output_pipeline_elements &elements) {
        return link_tee_to_branch(tee, elements.queue_rtp);
    }

    /**
     * @brief Adds WebRTC branch elements to pipeline.
     */
    void add_webrtc_branch_to_pipeline(const output_branch_facade &data, const output_pipeline_elements &elements) {
        gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_rtp, elements.rtp_payloader,
                         elements.webrtcbin, nullptr);
    }

    /**
     * @brief Sets up WebRTC signals and signaling client.
     */
    [[nodiscard]] bool setup_webrtc_signals(const output_branch_facade &data,
                                            const output_pipeline_elements &elements) {
        if (!elements.webrtcbin) {
            return true; // Not WebRTC, nothing to do
        }

        if (data.params.webrtc.signaling_url.empty()) {
            sf::error(output_pipeline, "WebRTC signaling URL not configured");
            return false;
        }

        // Create signaling client
        const sf::communication::signaling_message_callback callback =
                [elements](const sf::communication::signaling_message &msg) {
                    handle_signaling_message(elements, msg);
                };

        g_signaling_client = sf::communication::create_signaling_client(data.params.webrtc.signaling_url, callback);
        if (!g_signaling_client) {
            sf::error(output_pipeline, "Failed to create signaling client");
            return false;
        }

        if (!sf::communication::signaling_client_connect(g_signaling_client.get())) {
            sf::error(output_pipeline, "Failed to connect signaling client");
            return false;
        }

        if (!sf::communication::signaling_client_is_connected(g_signaling_client.get())) {
            sf::error(output_pipeline, "Signaling client connection verification failed");
            return false;
        }

        // Connect webrtcbin signals
        const gulong negotiation_signal_id = g_signal_connect(elements.webrtcbin, "on-negotiation-needed",
                                                              G_CALLBACK(on_negotiation_needed), nullptr);
        const gulong ice_signal_id =
                g_signal_connect(elements.webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), nullptr);
        const gulong ice_connection_state_signal_id =
                g_signal_connect(elements.webrtcbin, "notify::ice-connection-state",
                                 G_CALLBACK(on_ice_connection_state), &data.instance());

        if (negotiation_signal_id == 0) {
            sf::error(output_pipeline, "Failed to connect on-negotiation-needed signal");
            return false;
        }
        if (ice_signal_id == 0) {
            sf::error(output_pipeline, "Failed to connect on-ice-candidate signal");
            return false;
        }
        if (ice_connection_state_signal_id == 0) {
            sf::error(output_pipeline, "Failed to connect notify::ice-connection-state signal");
            return false;
        }

        sf::info(output_pipeline, "WebRTC signals configured for signaling: %s",
                 data.params.webrtc.signaling_url.c_str());
        return true;
    }
} // namespace

output_branch_functions create_webrtc_branch_functions() {
    return output_branch_functions{
            .is_enabled = [](const output_branch_facade &data) { return is_webrtc_branch_enabled(data); },
            .create_elements =
                    [](output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_webrtc_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return create_webrtc_branch_elements(data, elements);
                    },
            .configure_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_webrtc_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return configure_webrtc_branch_elements(data, elements);
                    },
            .link_branch =
                    [](const output_pipeline_elements &elements) {
                        if (!elements.webrtcbin) {
                            return true; // Not created, but not an error
                        }
                        return link_webrtc_branch(elements);
                    },
            .link_to_tee =
                    [](GstElement *tee, const output_pipeline_elements &elements) {
                        if (!elements.webrtcbin) {
                            return true; // Not created, but not an error
                        }
                        return link_tee_to_webrtc_branch(tee, elements);
                    },
            .add_to_pipeline =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (is_webrtc_branch_enabled(data)) {
                            add_webrtc_branch_to_pipeline(data, elements);
                        }
                    },
            .get_branch_name = []() { return "webrtc"; },
            .setup_signals =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (!is_webrtc_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return setup_webrtc_signals(data, elements);
                    }};
}

output_branch_context create_webrtc_branch_context() {
    return output_branch_context{.functions = create_webrtc_branch_functions()};
}
