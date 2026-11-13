// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "udp_branch.h"
#include "../../../../../libvcai/include/logging.h"
#include "../../../../encoder_enums.h"
#include "../../../pipeline_utils.h"
#include "../../../rtp_utils.h"
#include "../../elements.h"
#include "../../output_pipeline_debug.h"
#include "../output_branch_facade.h"

#include <gst/gst.h>
#include <optional>

/**
 * @file udp_branch.cpp
 * @brief Implementation of UDP RTP branch creation and linking.
 */

namespace {
    /**
     * @brief Tests if RTP plugins are available.
     * @return true if available, false otherwise.
     */
    [[nodiscard]] bool test_rtp_plugins_available() {
        GstElement *test_rtp_payloader = gst_element_factory_make("rtph265pay", "test-rtp-pay");
        GstElement *test_udp_sink = gst_element_factory_make("udpsink", "test-udp-sink");

        const bool available = test_rtp_payloader && test_udp_sink;

        if (test_rtp_payloader) {
            gst_object_unref(test_rtp_payloader);
        }
        if (test_udp_sink) {
            gst_object_unref(test_udp_sink);
        }

        return available;
    }

    /**
     * @brief Check if UDP branch should be enabled.
     * @param data The narrow output branch state view.
     * @return true if UDP branch should be enabled, false otherwise.
     */
    [[nodiscard]] bool is_udp_branch_enabled(const output_branch_facade &data) {
        return !data.params.communication.host.empty() && data.params.communication.port > 0 &&
               data.params.communication.resolved_transport_type() == sf::communication::transport_type::udp;
    }

    /**
     * @brief Performs communication handshake and creates UDP streaming elements.
     * @param data The narrow output branch state view.
     * @param elements Output structure to populate.
     * @return Handshake result if successful, std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<rtp_handshake_result>
    perform_handshake_and_create_udp_elements(const output_branch_facade &data, output_pipeline_elements &elements) {
        // Perform the actual handshake
        const auto handshake = perform_rtp_handshake(data.params.communication.host, data.params.communication.port,
                                                     "rtp", std::string());
        if (!handshake) {
            sf::error(output_pipeline, "Communication handshake failed, disabling UDP streaming");
            sf::error(output_pipeline, "Please ensure the communication server is properly configured and responding");
            return std::nullopt;
        }

        // UDP: queue -> rtph265pay -> udpsink
        elements.queue_rtp = gst_element_factory_make("queue", "q-rtp");
        elements.rtp_payloader = gst_element_factory_make("rtph265pay", "rtp-pay");
        elements.sink_rtp = gst_element_factory_make("udpsink", "s-rtp");

        if (!elements.queue_rtp || !elements.rtp_payloader || !elements.sink_rtp) {
            sf::error(output_pipeline, "Failed to create UDP elements");
            return std::nullopt;
        }

        return handshake;
    }

    /**
     * @brief Creates UDP branch elements.
     * @param data The narrow output branch state view (non-const to store assigned port).
     * @param elements Output structure to populate.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_udp_branch_elements(output_branch_facade &data, output_pipeline_elements &elements) {
        // Check for required plugins
        if (!test_rtp_plugins_available()) {
            sf::warning(output_pipeline, "RTP plugins not available, disabling UDP streaming");
            return false;
        }

        const auto handshake = perform_handshake_and_create_udp_elements(data, elements);
        if (!handshake) {
            return false;
        }

        // Store assigned port in global_data
        data.assigned_rtp_port.store(handshake->assigned_port);
        data.assigned_rtp_session_id.store(handshake->session_id);

        return true;
    }

    /**
     * @brief Configures UDP branch elements.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    [[nodiscard]] bool configure_udp_branch_elements(const output_branch_facade &data,
                                                     output_pipeline_elements &elements) {
        // Configure UDP branch queue
        if (elements.queue_rtp) {
            // Uniform queue size for synchronization across all branches
            const int effective_framerate = data.params.framerate > 0 ? data.params.framerate : 60;
            const int max_buffers = 2 * effective_framerate;
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_rtp, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured UDP branch queue: max-size-buffers=%d, leaky=no_leak", max_buffers);
        }

        // Configure RTP payloader for UDP: Set payload type 96 explicitly
        if (elements.rtp_payloader) {
            g_object_set(elements.rtp_payloader, "config-interval", 1, "pt", 96, nullptr);
        }

        // Configure UDP sink
        if (elements.sink_rtp) {
            const uint16_t rtp_port = data.assigned_rtp_port.load();
            g_object_set(elements.sink_rtp, "host", data.params.communication.host.c_str(), "port",
                         static_cast<int>(rtp_port), "async", false, "sync", false, nullptr);
            sf::info(output_pipeline, "UDP streaming configured: %s:%u", data.params.communication.host.c_str(),
                     rtp_port);
        }

        return true;
    }

    /**
     * @brief Links UDP branch elements.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_udp_branch(const output_pipeline_elements &elements) {
        return gst_element_link_many(elements.queue_rtp, elements.rtp_payloader, elements.sink_rtp, nullptr);
    }

    /**
     * @brief Links tee to UDP branch.
     * @param tee The tee element.
     * @param elements The pipeline elements.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool link_tee_to_udp_branch(GstElement *tee, const output_pipeline_elements &elements) {
        return link_tee_to_branch(tee, elements.queue_rtp);
    }

    /**
     * @brief Adds UDP branch elements to pipeline.
     * @param data The narrow output branch state view.
     * @param elements The pipeline elements.
     */
    void add_udp_branch_to_pipeline(const output_branch_facade &data, const output_pipeline_elements &elements) {
        gst_bin_add_many(GST_BIN(data.output_pipeline.get()), elements.queue_rtp, elements.rtp_payloader,
                         elements.sink_rtp, nullptr);
    }
} // namespace

output_branch_functions create_udp_branch_functions() {
    return output_branch_functions{
            .is_enabled = [](const output_branch_facade &data) { return is_udp_branch_enabled(data); },
            .create_elements =
                    [](output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_udp_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return create_udp_branch_elements(data, elements);
                    },
            .configure_elements =
                    [](const output_branch_facade &data, output_pipeline_elements &elements) {
                        if (!is_udp_branch_enabled(data)) {
                            return true; // Not enabled, but not an error
                        }
                        return configure_udp_branch_elements(data, elements);
                    },
            .link_branch =
                    [](const output_pipeline_elements &elements) {
                        if (!elements.queue_rtp) {
                            return true; // Not created, but not an error
                        }
                        return link_udp_branch(elements);
                    },
            .link_to_tee =
                    [](GstElement *tee, const output_pipeline_elements &elements) {
                        if (!elements.queue_rtp) {
                            return true; // Not created, but not an error
                        }
                        return link_tee_to_udp_branch(tee, elements);
                    },
            .add_to_pipeline =
                    [](const output_branch_facade &data, const output_pipeline_elements &elements) {
                        if (is_udp_branch_enabled(data)) {
                            add_udp_branch_to_pipeline(data, elements);
                        }
                    },
            .get_branch_name = []() { return "udp"; },
            .setup_signals =
                    [](const output_branch_facade &, const output_pipeline_elements &) {
                        return true; // No signals needed for UDP branch
                    }};
}

output_branch_context create_udp_branch_context() {
    return output_branch_context{.functions = create_udp_branch_functions()};
}
