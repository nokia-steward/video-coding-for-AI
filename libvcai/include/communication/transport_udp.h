// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <memory>
#include "transport.h"

namespace sf {
    namespace communication {

        /**
         * @file transport_udp.h
         * @brief UDP transport implementation.
         *
         * This module provides UDP-specific transport functions.
         * It wraps GLib's GSocket API for UDP communication.
         */

        /**
         * @struct udp_transport_data
         * @brief Internal data structure for UDP transport.
         *
         * This is an implementation detail and should not be accessed directly.
         */
        struct udp_transport_data;
        using udp_transport_data_ptr = std::unique_ptr<udp_transport_data, void (*)(udp_transport_data *)>;

        /**
         * @brief Creates UDP-specific transport data.
         * @return Owning UDP transport data handle, or nullptr on failure.
         */
        [[nodiscard]] udp_transport_data_ptr create_udp_transport_data();

        /**
         * @brief Destroys UDP transport data and cleans up resources.
         * @param data UDP transport data to destroy (can be nullptr).
         */
        void destroy_udp_transport_data(udp_transport_data *data);

        /**
         * @brief UDP-specific connect implementation.
         * @param data UDP transport data.
         * @param config Connection configuration.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error udp_connect_impl(udp_transport_data *data, const transport_config &config);

        /**
         * @brief UDP-specific send implementation.
         * @param data UDP transport data.
         * @param message The message to send.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error udp_send_impl(udp_transport_data *data, const protocol_message &message);

        /**
         * @brief UDP-specific receive implementation.
         * @param data UDP transport data.
         * @return Result containing the message or error code.
         */
        [[nodiscard]] transport_receive_result udp_receive_impl(udp_transport_data *data);

        /**
         * @brief UDP-specific disconnect implementation.
         * @param data UDP transport data.
         * @return transport_error::success on success, error code otherwise.
         */
        [[nodiscard]] transport_error udp_disconnect_impl(udp_transport_data *data);

        /**
         * @brief UDP-specific connection check.
         * @param data UDP transport data.
         * @return true if connected, false otherwise.
         */
        [[nodiscard]] bool udp_is_connected_impl(const udp_transport_data *data);

    } // namespace communication
} // namespace sf
