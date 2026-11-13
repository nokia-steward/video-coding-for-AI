// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoded_bridge_startup_contract.h"

#include "../../../libvcai/include/gst_raii.h"
#include "../../../libvcai/include/logging.h"
#include "../../encoder_cli_debug.h"

#include <gst/app/gstappsrc.h>

namespace {
    [[nodiscard]] GstElement *resolve_target_appsrc(encoded_stream_ports &ports, const encoded_bridge_target target) {
        if (target == encoded_bridge_target::encoding_src) {
            return ports.encoding_src;
        }
        return ports.sei_src;
    }

    [[nodiscard]] const char *target_name(const encoded_bridge_target target) {
        if (target == encoded_bridge_target::encoding_src) {
            return "encoding_src";
        }
        return "sei_src";
    }

    [[nodiscard]] bool is_encoding_target(const encoded_bridge_target target) {
        return target == encoded_bridge_target::encoding_src;
    }
} // namespace

bool configure_encoded_bridge_startup_contract(encoded_stream_ports &ports, encoded_bridge_runtime &egress_runtime,
                                               const encoded_bridge_target target, GstCaps *caps,
                                               const encoded_bridge_startup_contract_options &options) {
    if (caps == nullptr) {
        sf::error(encoder_cli_main, "Cannot configure startup contract for %s: caps are null", target_name(target));
        return false;
    }

    auto *const appsrc = resolve_target_appsrc(ports, target);
    if (appsrc == nullptr || !GST_IS_APP_SRC(appsrc)) {
        sf::error(encoder_cli_main, "Cannot configure startup contract for %s: appsrc is unavailable",
                  target_name(target));
        return false;
    }

    if (options.max_bytes.has_value()) {
        g_object_set(appsrc, "caps", caps, "format", GST_FORMAT_TIME, "is-live", options.is_live, "do-timestamp",
                     options.do_timestamp, "block", options.block ? TRUE : FALSE, "max-bytes", *options.max_bytes,
                     nullptr);
    } else {
        g_object_set(appsrc, "caps", caps, "format", GST_FORMAT_TIME, "is-live", options.is_live, "do-timestamp",
                     options.do_timestamp, "block", options.block ? TRUE : FALSE, nullptr);
    }

    const bool encoding_target = is_encoding_target(target);
    if (!egress_runtime.mark_startup_configured_once(encoding_target)) {
        sf::error(encoder_cli_main, "Startup contract for %s was already configured; refusing second startup lock",
                  target_name(target));
        return false;
    }

    if (!egress_runtime.lock_caps_contract(encoding_target, caps)) {
        sf::error(encoder_cli_main, "Failed to lock startup caps contract for %s", target_name(target));
        return false;
    }

    sf::unique_g_char_ptr caps_text(gst_caps_to_string(caps));
    sf::info(encoder_cli_main, "Established appsrc caps lock for %s: %s", target_name(target),
             caps_text ? caps_text.get() : "null");
    return true;
}
