// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gio/gio.h>
#include <map>
#include <string>
#include <utility>

#include "../../include/communication/signaling.h"

namespace sf::communication {
    /**
     * @brief Server-side signaling state and connected client resources.
     */
    struct signaling_server {
        uint16_t port;
        bool use_ssl;
        signaling_client_connected_callback client_connected_callback;
        signaling_client_disconnected_callback client_disconnected_callback;
        signaling_server_message_callback message_callback;
        GSocketListener *listener;
        GSocketService *service;
        GMainContext *context;
        std::map<std::string, GSocketConnection *> client_connections;
        std::map<std::string, GSocket *> client_sockets;
        std::map<std::string, GIOChannel *> client_channels;
        std::map<std::string, guint> client_watches;

        signaling_server(uint16_t port_param, bool use_ssl_param,
                         signaling_client_connected_callback client_connected_callback_param,
                         signaling_client_disconnected_callback client_disconnected_callback_param,
                         signaling_server_message_callback message_callback_param) :
            port(port_param),
            use_ssl(use_ssl_param), client_connected_callback(std::move(client_connected_callback_param)),
            client_disconnected_callback(std::move(client_disconnected_callback_param)),
            message_callback(std::move(message_callback_param)), listener(nullptr), service(nullptr), context(nullptr) {
        }
    };
} // namespace sf::communication
