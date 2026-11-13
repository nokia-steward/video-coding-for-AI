// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rtp_branch.h"
#include "../../../../libvcai/include/gst_raii.h"
#include "../../../../libvcai/include/logging.h"
#include "../../global_data.h"
#include "output_pipeline_debug.h"

#include <gst/gst.h>

/**
 * @file rtp_branch.cpp
 * @brief Backward-compatible startup hook for WebRTC negotiation checks.
 */

bool trigger_webrtc_negotiation(const global_data &data) {
    if (data.params.communication.resolved_transport_type() != sf::communication::transport_type::webrtc) {
        return true;
    }

    if (!data.output_pipeline) {
        sf::error(output_pipeline, "Output pipeline not available for WebRTC negotiation");
        return false;
    }

    const sf::unique_gst_element_ptr webrtcbin(gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "webrtcbin"));
    if (!webrtcbin) {
        sf::warning(output_pipeline, "webrtcbin element not found in output pipeline");
        return false;
    }

    // Negotiation is handled by output_branches/webrtc signal wiring.
    sf::info(output_pipeline, "WebRTC negotiation is signal-driven; manual trigger skipped");
    return true;
}
