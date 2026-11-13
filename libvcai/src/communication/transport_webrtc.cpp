// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../include/communication/transport_webrtc.h"
#include "../../include/communication/protocol.h"
#include "../../include/communication/signaling.h"

#include <chrono>
#include <condition_variable>
#include <gio/gio.h>
#include <gst/gst.h>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include "../../include/logging.h"

namespace sf::communication {
    namespace {
        // Debug category for WebRTC transport implementation.
        GST_DEBUG_CATEGORY_STATIC(transport_webrtc);
#define GST_CAT_DEFAULT transport_webrtc

        /**
         * @brief Initialize the WebRTC transport debug category once.
         */
        void init_debug_category() {
            static bool initialized = false;
            if (!initialized) {
                GST_DEBUG_CATEGORY_INIT(transport_webrtc, "transport-webrtc", 0, "WebRTC transport implementation");
                initialized = true;
            }
        }
    } // namespace

    /**
     * @struct webrtc_transport_data
     * @brief Holds signaling state and synchronization for WebRTC transport.
     */
    struct webrtc_transport_data {
        std::unique_ptr<signaling_client, void (*)(signaling_client *)> signaling_client_ptr;
        webrtc_config webrtc_config_data;
        bool connected;
        std::optional<protocol_message> pending_message; // For receive operations
        std::mutex message_mutex; // For thread-safe access to pending_message
        std::condition_variable message_cv; // For waiting on message arrival

        /**
         * @brief Construct a WebRTC transport state with defaults.
         */
        webrtc_transport_data() :
            signaling_client_ptr(nullptr, destroy_signaling_client), webrtc_config_data{}, connected(false),
            pending_message(std::nullopt), message_mutex(), message_cv() {}
    };

    webrtc_transport_data_ptr create_webrtc_transport_data() {
        init_debug_category();
        return webrtc_transport_data_ptr(new webrtc_transport_data{}, destroy_webrtc_transport_data);
    }

    /**
     * @brief Sets WebRTC configuration in transport data.
     * @param data WebRTC transport data.
     * @param webrtc_config_param WebRTC configuration to set.
     */
    void set_webrtc_config(webrtc_transport_data *data, const webrtc_config &webrtc_config_param) {
        if (!data) {
            return;
        }
        data->webrtc_config_data = webrtc_config_param;
    }

    namespace {
        /**
         * @brief Handles signaling messages received from the signaling server.
         * @param data WebRTC transport data.
         * @param msg Signaling message received.
         */
        void handle_signaling_message(webrtc_transport_data *data, const signaling_message &msg) {
            init_debug_category();
            debug(transport_webrtc, "Signaling callback invoked with message type: %d, data size: %zu",
                  static_cast<int>(msg.type), msg.data.size());

            // Handle protocol messages (handshake, heartbeat, disconnect) by storing for receive operation
            if (msg.type == signaling_message_type::handshake || msg.type == signaling_message_type::heartbeat ||
                msg.type == signaling_message_type::disconnect) {
                info(transport_webrtc, "Received protocol message via signaling (type: %d), deserializing...",
                     static_cast<int>(msg.type));
                // Deserialize protocol message and store for receive
                const std::vector<uint8_t> msg_data(msg.data.begin(), msg.data.end());
                debug(transport_webrtc, "Message data size: %zu bytes", msg_data.size());
                const auto protocol_msg_opt = deserialize_message(msg_data);
                if (protocol_msg_opt) {
                    info(transport_webrtc, "Successfully deserialized protocol message, type: %d, storing for receive",
                         static_cast<int>(protocol_msg_opt->type));
                    std::lock_guard<std::mutex> lock(data->message_mutex);
                    data->pending_message = protocol_msg_opt.value();
                    debug(transport_webrtc, "Notifying condition variable for waiting receive operation");
                    data->message_cv.notify_one(); // Notify waiting receive operation
                    info(transport_webrtc, "Condition variable notified, message stored in pending_message");
                } else {
                    error(transport_webrtc, "Failed to deserialize protocol message from signaling data");
                }
            } else {
                // SDP/ICE messages are handled by webrtcbin, not here
                debug(transport_webrtc, "Received signaling message type: %d (not a protocol message)",
                      static_cast<int>(msg.type));
            }
        }
    } // namespace

    void destroy_webrtc_transport_data(webrtc_transport_data *data) {
        if (!data) {
            return;
        }

        init_debug_category();

        if (data->connected) {
            // Ignore return value during cleanup - we're destroying the object anyway
            [[maybe_unused]] const transport_error disconnect_result = webrtc_disconnect_impl(data);
        }

        // signaling_client_ptr will be destroyed automatically via unique_ptr.
        std::unique_ptr<webrtc_transport_data>{data};
    }

    transport_error webrtc_connect_impl(webrtc_transport_data *data, const transport_config &config,
                                        const webrtc_config &webrtc_config_param) {
        if (!data) {
            return transport_error::unknown_error;
        }

        init_debug_category();
        info(transport_webrtc, "webrtc_connect_impl called with config host=%s, port=%u, signaling_url=%s",
             config.host.c_str(), config.port, webrtc_config_param.signaling_url.c_str());

        // Clean up any existing connection
        if (data->connected) {
            info(transport_webrtc, "Cleaning up existing connection before reconnecting...");
            // Ignore return value - we're reconnecting anyway
            [[maybe_unused]] const transport_error disconnect_result = webrtc_disconnect_impl(data);
        }

        // Use provided webrtc_config_param, or fall back to existing one in data if param is empty
        if (!webrtc_config_param.signaling_url.empty()) {
            info(transport_webrtc, "Using provided webrtc_config_param with signaling_url: %s",
                 webrtc_config_param.signaling_url.c_str());
            data->webrtc_config_data = webrtc_config_param;
        } else if (data->webrtc_config_data.signaling_url.empty()) {
            // Both are empty - create minimal config from transport_config
            data->webrtc_config_data.signaling_url = "ws://" + config.host + ":" + std::to_string(config.port);
            data->webrtc_config_data.ice_timeout_ms = static_cast<uint32_t>(config.timeout.count());
            info(transport_webrtc, "Created signaling_url from transport_config: %s",
                 data->webrtc_config_data.signaling_url.c_str());
        } else {
            info(transport_webrtc, "Using existing webrtc_config_data with signaling_url: %s",
                 data->webrtc_config_data.signaling_url.c_str());
        }
        // Use data->webrtc_config_data (which is now set)
        const webrtc_config &webrtc_cfg = data->webrtc_config_data;
        info(transport_webrtc, "Final webrtc_config signaling_url: %s", webrtc_cfg.signaling_url.c_str());

        // Create signaling client using the webrtc_config
        signaling_message_callback callback = [data](const signaling_message &msg) {
            handle_signaling_message(data, msg);
        };

        info(transport_webrtc, "Creating signaling client with URL: %s", webrtc_cfg.signaling_url.c_str());
        data->signaling_client_ptr = create_signaling_client(webrtc_cfg.signaling_url, callback);
        if (!data->signaling_client_ptr) {
            error(transport_webrtc, "Failed to create signaling client");
            return transport_error::connection_failed;
        }
        info(transport_webrtc, "Signaling client created successfully");

        // Connect signaling client
        info(transport_webrtc, "Connecting signaling client...");
        if (!signaling_client_connect(data->signaling_client_ptr.get())) {
            error(transport_webrtc, "Failed to connect signaling client");
            return transport_error::connection_failed;
        }

        data->connected = true;
        info(transport_webrtc, "WebRTC transport connected via signaling: %s", webrtc_cfg.signaling_url.c_str());

        return transport_error::success;
    }

    transport_error webrtc_send_impl(webrtc_transport_data *data, const protocol_message &message) {
        if (!data || !data->connected || !data->signaling_client_ptr) {
            error(transport_webrtc, "Send called but transport not connected");
            return transport_error::not_connected;
        }

        init_debug_category();
        info(transport_webrtc, "Sending protocol message via WebRTC signaling, type: %d",
             static_cast<int>(message.type));

        // Serialize protocol message
        const std::vector<uint8_t> serialized = serialize_message(message);
        const std::string message_data(serialized.begin(), serialized.end());
        debug(transport_webrtc, "Serialized message size: %zu bytes", serialized.size());

        // Determine signaling message type based on protocol message type
        signaling_message_type sig_type;
        switch (message.type) {
            case message_type::handshake_request:
            case message_type::handshake_response:
                sig_type = signaling_message_type::handshake;
                break;
            case message_type::heartbeat:
                sig_type = signaling_message_type::heartbeat;
                break;
            case message_type::disconnect_request:
            case message_type::disconnect_ack:
                sig_type = signaling_message_type::disconnect;
                break;
            default:
                error(transport_webrtc, "Unsupported protocol message type for WebRTC: %d",
                      static_cast<int>(message.type));
                return transport_error::invalid_message;
        }

        debug(transport_webrtc, "Mapped protocol message type %d to signaling message type %d",
              static_cast<int>(message.type), static_cast<int>(sig_type));

        signaling_message sig_msg{sig_type, message_data};

        if (!signaling_client_send(data->signaling_client_ptr.get(), sig_msg)) {
            error(transport_webrtc, "Failed to send protocol message via signaling");
            return transport_error::send_failed;
        }

        info(transport_webrtc, "Successfully sent protocol message via WebRTC signaling");
        return transport_error::success;
    }

    namespace {
        /**
         * @brief Pumps pending events in the main context.
         * @param main_context GLib main context to pump.
         */
        void pump_main_context(GMainContext *main_context) {
            while (g_main_context_pending(main_context)) {
                g_main_context_iteration(main_context, FALSE);
            }
        }

        /**
         * @brief Checks for pending message and returns it if available.
         * @param data WebRTC transport data.
         * @param lock Lock guard for message mutex (must be locked).
         * @return Message if available, empty optional otherwise.
         */
        [[nodiscard]] std::optional<protocol_message> check_pending_message(webrtc_transport_data *data,
                                                                            std::unique_lock<std::mutex> &lock) {
            if (data->pending_message.has_value()) {
                const protocol_message msg = data->pending_message.value();
                data->pending_message = std::nullopt;
                return msg;
            }
            return std::nullopt;
        }

        /**
         * @brief Waits for a message to arrive with timeout, pumping main context periodically.
         * @param data WebRTC transport data.
         * @param timeout_duration Maximum time to wait.
         * @return Message if received, empty optional on timeout.
         */
        [[nodiscard]] std::optional<protocol_message>
        wait_for_message_with_timeout(webrtc_transport_data *data, std::chrono::milliseconds timeout_duration) {
            const auto start_time = std::chrono::steady_clock::now();
            std::unique_lock<std::mutex> lock(data->message_mutex);

            // Get the default main context to pump events during wait
            GMainContext *main_context = g_main_context_default();
            const bool created_context = (main_context == nullptr);
            if (created_context) {
                main_context = g_main_context_new();
            }

            // Wait for message to arrive or timeout, pumping main loop periodically
            while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
                // Release lock before pumping main context to allow callback to acquire lock
                lock.unlock();

                // Pump pending events in the main context (this allows IO watch callbacks to run)
                pump_main_context(main_context);

                // Re-acquire lock to check for message
                lock.lock();

                // Check if message arrived
                const auto msg_opt = check_pending_message(data, lock);
                if (msg_opt) {
                    lock.unlock();
                    if (created_context) {
                        g_main_context_unref(main_context);
                    }
                    return msg_opt;
                }

                // Wait a short time before checking again
                const auto elapsed = std::chrono::steady_clock::now() - start_time;
                const auto remaining_time =
                        timeout_duration - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
                if (remaining_time <= std::chrono::milliseconds(0)) {
                    break;
                }
                const auto wait_time = std::min(remaining_time, std::chrono::milliseconds(100));
                data->message_cv.wait_for(lock, wait_time, [data] { return data->pending_message.has_value(); });
            }

            lock.unlock();

            if (created_context) {
                g_main_context_unref(main_context);
            }

            return std::nullopt;
        }
    } // namespace

    transport_receive_result webrtc_receive_impl(webrtc_transport_data *data) {
        if (!data || !data->connected || !data->signaling_client_ptr) {
            error(transport_webrtc, "Receive called but transport not connected");
            return transport_receive_result::failure(transport_error::not_connected);
        }

        init_debug_category();
        info(transport_webrtc, "Starting receive operation, waiting for message...");

        // Check if there's already a pending message
        {
            std::unique_lock<std::mutex> lock(data->message_mutex);
            const auto msg_opt = check_pending_message(data, lock);
            if (msg_opt) {
                info(transport_webrtc, "Found existing pending message, returning immediately");
                return transport_receive_result::success(msg_opt.value());
            }
        }

        // Wait for message with timeout (default 5 seconds)
        const auto timeout_duration = std::chrono::milliseconds(5000);
        info(transport_webrtc, "No pending message, waiting on condition variable (timeout: %ld ms)...",
             timeout_duration.count());

        const auto msg_opt = wait_for_message_with_timeout(data, timeout_duration);

        if (msg_opt) {
            info(transport_webrtc, "Message received! Type: %d", static_cast<int>(msg_opt->type));
            return transport_receive_result::success(msg_opt.value());
        }

        // Timeout - no message received
        error(transport_webrtc, "Receive timeout - no message received within %ld ms", timeout_duration.count());
        return transport_receive_result::failure(transport_error::timeout);
    }

    transport_error webrtc_disconnect_impl(webrtc_transport_data *data) {
        if (!data) {
            return transport_error::unknown_error;
        }

        init_debug_category();

        if (data->signaling_client_ptr) {
            signaling_client_disconnect(data->signaling_client_ptr.get());
        }

        data->connected = false;
        data->pending_message = std::nullopt;

        info(transport_webrtc, "WebRTC transport disconnected");
        return transport_error::success;
    }

    bool webrtc_is_connected_impl(const webrtc_transport_data *data) {
        if (!data) {
            return false;
        }
        return data->connected && data->signaling_client_ptr &&
               signaling_client_is_connected(data->signaling_client_ptr.get());
    }

} // namespace sf::communication
