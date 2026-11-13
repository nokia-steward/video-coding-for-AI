// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "transport.h"

#include <memory>
#include <string>

namespace sf {
    namespace communication {

        /**
         * @file transport_webrtc.h
         * @brief WebRTC transport implementation.
         *
         * This module provides WebRTC-specific transport functions.
         * It uses GStreamer's webrtcbin element and WebSocket for signaling.
         */

        /**
         * @struct webrtc_config
         * @brief WebRTC-specific configuration.
         */
        struct webrtc_config {
            std::string signaling_url; ///< WebSocket URL for signaling
            std::string stun_server; ///< STUN server URL (optional)
            std::string turn_server; ///< TURN server URL (optional)
            std::string turn_username; ///< TURN username (optional)
            std::string turn_password; ///< TURN password (optional)
            uint32_t ice_timeout_ms; ///< ICE candidate timeout
            bool enable_data_channels; ///< Enable data channels
        };

        /**
         * @struct webrtc_transport_data
         * @brief Internal data structure for WebRTC transport.
         *
         * This is an implementation detail and should not be accessed directly.
         */
        struct webrtc_transport_data;
        using webrtc_transport_data_ptr = std::unique_ptr<webrtc_transport_data, void (*)(webrtc_transport_data *)>;

        /**
         * @brief Creates WebRTC-specific transport data.
         * @return Owning WebRTC transport data handle, or nullptr on failure.
         */
        [[nodiscard]] webrtc_transport_data_ptr create_webrtc_transport_data();

        /**
         * @brief Destroys WebRTC transport data and cleans up resources.
         * @param data WebRTC transport data to destroy (can be nullptr).
         */
        void destroy_webrtc_transport_data(webrtc_transport_data *data);

        /**
         * @brief Sets WebRTC configuration in transport data.
         * @param data WebRTC transport data.
         * @param webrtc_config_param WebRTC configuration to set.
         */
        void set_webrtc_config(webrtc_transport_data *data, const webrtc_config &webrtc_config_param);

        /**
         * @brief WebRTC-specific connect implementation.
         * @param data WebRTC transport data.
         * @param config Connection configuration.
         * @param webrtc_config WebRTC-specific configuration.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error webrtc_connect_impl(webrtc_transport_data *data, const transport_config &config,
                                                          const webrtc_config &webrtc_config);

        /**
         * @brief WebRTC-specific send implementation.
         * @param data WebRTC transport data.
         * @param message The message to send.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error webrtc_send_impl(webrtc_transport_data *data, const protocol_message &message);

        /**
         * @brief WebRTC-specific receive implementation.
         * @param data WebRTC transport data.
         * @return Result containing the message or error code.
         */
        [[nodiscard]] transport_receive_result webrtc_receive_impl(webrtc_transport_data *data);

        /**
         * @brief WebRTC-specific disconnect implementation.
         * @param data WebRTC transport data.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error webrtc_disconnect_impl(webrtc_transport_data *data);

        /**
         * @brief WebRTC-specific connection check.
         * @param data WebRTC transport data.
         * @return true if connected, false otherwise.
         */
        [[nodiscard]] bool webrtc_is_connected_impl(const webrtc_transport_data *data);

    } // namespace communication
} // namespace sf
