// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "webrtc_signaling.h"

#include <gio/gio.h>
#include <glib.h>
#include <gmodule.h>
#include <gst/gst.h>
#include <gst/sdp/sdp.h>

#include "../../pipeline/context/pipeline_contexts.h"
#include "logging.h"

// Reuse the server debug category defined in server.cpp.
GST_DEBUG_CATEGORY_EXTERN(server_category);
#define GST_CAT_DEFAULT server_category

namespace {
    /**
     * @struct webrtc_session_description
     * @brief Minimal representation of a WebRTC SDP description for GStreamer interop.
     */
    struct webrtc_session_description {
        int type; // GstWebRTCSDPType
        GstSDPMessage *sdp;
    };

    /**
     * @brief GstWebRTCSDPType value for SDP offers.
     */
    constexpr int WEBRTC_SDP_TYPE_OFFER = 1;

    /**
     * @struct set_remote_desc_data
     * @brief Aggregates callback state for setting remote descriptions.
     */
    struct set_remote_desc_data {
        transport_build_context *context;
        GstElement *webrtcbin_elem; // Non-owning: owned by pipeline element graph.
        std::string client_id;
        sf::communication::signaling_server *signaling_server; // Non-owning.
    };

    /**
     * @brief Function pointer type for gst_webrtc_session_description_new.
     */
    using create_session_desc_func = webrtc_session_description *(*) (int, GstSDPMessage *);

    /**
     * @brief Resolve gst_webrtc_session_description_new symbol lazily.
     * @return Function pointer if available, nullptr otherwise.
     */
    [[nodiscard]] create_session_desc_func get_webrtc_session_description_new_func() {
        static create_session_desc_func func = nullptr;
        static bool initialized = false;
        if (!initialized) {
            initialized = true;
            GModule *module = g_module_open(nullptr, G_MODULE_BIND_LAZY);
            if (module) {
                gpointer symbol = nullptr;
                if (g_module_symbol(module, "gst_webrtc_session_description_new", &symbol)) {
                    func = reinterpret_cast<create_session_desc_func>(symbol);
                }
                // Intentionally keep module open: this is the main process module.
            }
        }
        return func;
    }

    /**
     * @brief Handle the create-answer promise callback.
     * @param answer_promise_param Promise object.
     * @param answer_user_data User data (set_remote_desc_data pointer).
     */
    void handle_create_answer_promise(GstPromise *answer_promise_param, gpointer answer_user_data) {
        const auto *answer_data = static_cast<set_remote_desc_data *>(answer_user_data);
        const GstStructure *answer_reply = gst_promise_get_reply(answer_promise_param);

        if (!answer_reply) {
            sf::error(server_category, "create-answer promise reply is null for client %s",
                      answer_data->client_id.c_str());
        } else if (gst_structure_has_name(answer_reply, "error")) {
            const gchar *error_message = gst_structure_get_string(answer_reply, "error");
            sf::error(server_category, "create-answer failed for client %s: %s", answer_data->client_id.c_str(),
                      error_message ? error_message : "unknown error");
        } else {
            const GValue *answer_value = gst_structure_get_value(answer_reply, "answer");
            if (!answer_value) {
                sf::error(server_category, "create-answer reply missing 'answer' field for client %s",
                          answer_data->client_id.c_str());
            } else {
                auto *answer_desc = static_cast<webrtc_session_description *>(g_value_get_boxed(answer_value));
                if (!answer_desc) {
                    sf::error(server_category, "create-answer returned null session description for client %s",
                              answer_data->client_id.c_str());
                } else if (!answer_desc->sdp) {
                    sf::error(server_category, "Answer SDP message is null for client %s",
                              answer_data->client_id.c_str());
                } else {
                    GstSDPMessage *answer_sdp_copy_for_local = nullptr;
                    if (gst_sdp_message_copy(answer_desc->sdp, &answer_sdp_copy_for_local) == GST_SDP_OK &&
                        answer_sdp_copy_for_local) {
                        constexpr int WEBRTC_SDP_TYPE_ANSWER_LOCAL = 3;
                        auto *answer_for_local = g_new0(webrtc_session_description, 1);
                        if (answer_for_local) {
                            answer_for_local->type = WEBRTC_SDP_TYPE_ANSWER_LOCAL;
                            answer_for_local->sdp = answer_sdp_copy_for_local;
                            g_signal_emit_by_name(answer_data->webrtcbin_elem, "set-local-description",
                                                  answer_for_local, nullptr);
                        } else {
                            gst_sdp_message_free(answer_sdp_copy_for_local);
                            sf::error(server_category, "Failed to allocate webrtc_session_description");
                        }
                    } else {
                        sf::error(server_category, "Failed to copy SDP answer for set-local-description");
                    }

                    GstSDPMessage *sdp_message_copy = nullptr;
                    if (gst_sdp_message_copy(answer_desc->sdp, &sdp_message_copy) == GST_SDP_OK && sdp_message_copy) {
                        gchar *sdp_string = gst_sdp_message_as_text(sdp_message_copy);
                        if (sdp_string) {
                            const sf::communication::signaling_message answer_message{
                                    sf::communication::signaling_message_type::sdp_answer, std::string(sdp_string)};

                            if (answer_data->signaling_server &&
                                sf::communication::signaling_server_send_to_client(
                                        answer_data->signaling_server, answer_data->client_id, answer_message)) {
                                sf::info(server_category, "Sent SDP answer to client %s",
                                         answer_data->client_id.c_str());
                            } else {
                                sf::error(server_category, "Failed to send SDP answer to client %s",
                                          answer_data->client_id.c_str());
                            }

                            g_free(sdp_string);
                        }
                        gst_sdp_message_free(sdp_message_copy);
                    } else {
                        sf::error(server_category, "Failed to copy SDP answer message for client %s",
                                  answer_data->client_id.c_str());
                    }
                }
            }
        }
        gst_promise_unref(answer_promise_param);
        delete answer_data;
    }

    /**
     * @brief Handle the set-remote-description promise callback.
     * @param promise_param Promise object.
     * @param user_data_param User data (set_remote_desc_data pointer).
     */
    void handle_set_remote_description_promise(GstPromise *promise_param, gpointer user_data_param) {
        auto *data = static_cast<set_remote_desc_data *>(user_data_param);
        const GstStructure *reply = gst_promise_get_reply(promise_param);

        if (reply && gst_structure_has_name(reply, "error")) {
            const gchar *error_message = gst_structure_get_string(reply, "error");
            sf::error(server_category, "Failed to set remote description: %s",
                      error_message ? error_message : "unknown error");
            delete data;
            gst_promise_unref(promise_param);
            return;
        }

        GstPromise *answer_promise = gst_promise_new_with_change_func(handle_create_answer_promise, data, nullptr);
        if (answer_promise) {
            g_signal_emit_by_name(data->webrtcbin_elem, "create-answer", nullptr, answer_promise);
        } else {
            sf::error(server_category, "Failed to create promise for create-answer");
            delete data;
        }

        gst_promise_unref(promise_param);
    }
} // namespace

void handle_webrtc_signaling_message(transport_build_context *context, const sf::communication::signaling_message &msg,
                                     const std::string &client_id,
                                     sf::communication::signaling_server *signaling_server) {
    if (!context || !context->webrtcbin) {
        sf::warning(server_category, "Received signaling message for invalid pipeline");
        return;
    }

    if (msg.type == sf::communication::signaling_message_type::sdp_offer) {
        sf::info(server_category, "Received SDP offer from client %s (%zu bytes)", client_id.c_str(), msg.data.size());

        GstSDPMessage *sdp_message = nullptr;
        if (gst_sdp_message_new(&sdp_message) == GST_SDP_OK) {
            if (gst_sdp_message_parse_buffer(reinterpret_cast<const guint8 *>(msg.data.c_str()),
                                             static_cast<guint>(msg.data.size()), sdp_message) == GST_SDP_OK) {
                auto *callback_data =
                        new set_remote_desc_data{context, context->webrtcbin.get(), client_id, signaling_server};

                const GType webrtc_sdp_type = g_type_from_name("GstWebRTCSessionDescription");
                if (webrtc_sdp_type == G_TYPE_INVALID) {
                    sf::error(server_category, "GstWebRTCSessionDescription type not found");
                    gst_sdp_message_free(sdp_message);
                    delete callback_data;
                    return;
                }

                const auto create_func = get_webrtc_session_description_new_func();
                webrtc_session_description *offer_desc = nullptr;
                if (create_func) {
                    offer_desc = create_func(WEBRTC_SDP_TYPE_OFFER, sdp_message);
                } else {
                    offer_desc = g_new0(webrtc_session_description, 1);
                    if (offer_desc) {
                        offer_desc->type = WEBRTC_SDP_TYPE_OFFER;
                        offer_desc->sdp = sdp_message;
                    }
                }

                if (!offer_desc) {
                    sf::error(server_category, "Failed to create GstWebRTCSessionDescription");
                    gst_sdp_message_free(sdp_message);
                    delete callback_data;
                    return;
                }

                GstPromise *promise_local =
                        gst_promise_new_with_change_func(handle_set_remote_description_promise, callback_data, nullptr);
                g_signal_emit_by_name(context->webrtcbin.get(), "set-remote-description", offer_desc, promise_local);
                return;
            }

            sf::error(server_category, "Failed to parse SDP offer from client %s", client_id.c_str());
            gst_sdp_message_free(sdp_message);
            return;
        }

        sf::error(server_category, "Failed to create SDP message structure");
        return;
    }

    if (msg.type == sf::communication::signaling_message_type::ice_candidate) {
        g_signal_emit_by_name(context->webrtcbin.get(), "add-ice-candidate", 0, msg.data.c_str());
    }
}
