// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gio/gio.h>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "../../include/communication/signaling.h"

namespace sf::communication {
    /**
     * @brief Client-side signaling state and resources.
     */
    struct signaling_client {
        std::string url;
        signaling_message_callback message_callback;
        GSocket *socket;
        GSocketAddress *server_address;
        GIOChannel *io_channel;
        guint io_watch_id;
        bool connected;
        std::unique_ptr<std::jthread> receive_thread;
        std::atomic<bool> should_stop{false};

        signaling_client(std::string url_param, signaling_message_callback callback_param) :
            url(std::move(url_param)), message_callback(std::move(callback_param)), socket(nullptr),
            server_address(nullptr), io_channel(nullptr), io_watch_id(0), connected(false), receive_thread(nullptr) {}
    };
} // namespace sf::communication
