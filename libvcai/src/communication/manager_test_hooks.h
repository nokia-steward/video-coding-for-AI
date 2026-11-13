// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../include/communication/transport.h"

namespace sf::communication {
    using manager_create_transport_fn =
            std::unique_ptr<transport_handle, void (*)(transport_handle *)> (*)(transport_type);
    using manager_transport_get_type_fn = transport_type (*)(const transport_handle *);
    using manager_transport_set_webrtc_config_fn = void (*)(transport_handle *, const webrtc_config &);
    using manager_transport_connect_fn = transport_error (*)(transport_handle *, const transport_config &);
    using manager_transport_send_fn = transport_error (*)(transport_handle *, const protocol_message &);
    using manager_transport_receive_fn = transport_receive_result (*)(transport_handle *);
    using manager_transport_disconnect_fn = transport_error (*)(transport_handle *);

    /**
     * @brief Overrides communication-manager transport operations for tests.
     *
     * Any nullptr hook falls back to the default production transport function.
     */
    void manager_test_set_transport_hooks(manager_create_transport_fn create_transport_fn,
                                          manager_transport_get_type_fn transport_get_type_fn,
                                          manager_transport_set_webrtc_config_fn transport_set_webrtc_config_fn,
                                          manager_transport_connect_fn transport_connect_fn,
                                          manager_transport_send_fn transport_send_fn,
                                          manager_transport_receive_fn transport_receive_fn,
                                          manager_transport_disconnect_fn transport_disconnect_fn);

    /**
     * @brief Restores default production transport operations.
     */
    void manager_test_reset_transport_hooks();
} // namespace sf::communication
