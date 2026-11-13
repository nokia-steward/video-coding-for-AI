// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "transport_factory.h"
#include "logging.h"
#include "rtp_input/rtp_input_transport.h"
#include "webrtc/webrtc_transport.h"

// Debug category for transport factory operations.
GST_DEBUG_CATEGORY_STATIC(transport_factory_category);
#define GST_CAT_DEFAULT transport_factory_category

transport_context create_transport_context(transport_type transport_type, const rtp_server_config *config) {
    // Initialize debug category if not already initialized
    GST_DEBUG_CATEGORY_INIT(transport_factory_category, "transport-factory", 0, "Transport Factory");

    switch (transport_type) {
        case transport_type::rtp:
            return create_rtp_input_transport_context();

        case transport_type::webrtc:
            if (!config) {
                // Invalid: WebRTC requires config
                sf::error(transport_factory_category, "WebRTC transport requires config");
                return {.functions = {}};
            }
            return create_webrtc_transport_context(config);

        default:
            sf::error(transport_factory_category, "Unknown transport type: %d", static_cast<int>(transport_type));
            return {.functions = {}};
    }
}

void free_transport_context(transport_context *context) {
    // With std::function, no manual cleanup needed - lambdas are automatically
    // destroyed when the context goes out of scope
    (void) context;
}
