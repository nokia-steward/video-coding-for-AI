// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "api.h"
#include "../../../libvcai/include/gst_raii.h"
#include "../../../libvcai/include/logging.h"
#include "../../bridge/orchestration/encoded_bridge_startup_contract.h"
#include "../../encoder_enums.h"
#include "../../global_data.h"
#include "../rtp_heartbeat.h"
#include "app/output_branch_controller.h"
#include "elements.h"
#include "output_pipeline_debug.h"
#include "paths.h"
#include "probes.h"

#include <gst/gst.h>

/**
 * @file api.cpp
 * @brief Implementation of output pipeline creation and management API.
 */

namespace {
    /**
     * @brief Creates and configures SEI source element.
     * @param data The global data structure.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_sei_source(global_data &data) {
        data.encoded_ports.sei_src = gst_element_factory_make("appsrc", "sei_src");
        if (!data.encoded_ports.sei_src) {
            sf::error(output_pipeline, "SEI appsrc element could not be created.");
            return false;
        }

        const sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING,
                                                               "byte-stream", "alignment", G_TYPE_STRING, "au",
                                                               nullptr));
        // In random access mode: do-timestamp=false to use timestamps from encoded buffers
        // This is critical for random access mode where frames arrive in batches with specific timestamps
        // In low delay mode: keep default behavior (do-timestamp=true) to maintain compatibility
        const bool is_random_access = data.params.packer_type_value == packer_type::random_access;
        const encoded_bridge_startup_contract_options startup_options{
                .is_live = !is_random_access,
                .do_timestamp = !is_random_access,
                .block = true,
                .max_bytes = 0,
        };
        if (!configure_encoded_bridge_startup_contract(data.encoded_ports, data.egress_bridge_runtime,
                                                       encoded_bridge_target::sei_src, caps.get(), startup_options)) {
            sf::error(output_pipeline, "Failed to establish startup contract for sei_src");
            return false;
        }
        sf::info(output_pipeline, "Configured sei_src: is-live=%s, do-timestamp=%s (packer_type=%s)",
                 is_random_access ? "false" : "true", is_random_access ? "false" : "true",
                 packer_type_to_string(data.params.packer_type_value));

        return true;
    }

} // namespace

namespace {
    /**
     * @brief Creates pipeline and SEI source.
     * @param data Global data.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool create_pipeline_and_sei_source(global_data &data) {
        data.output_pipeline.reset(gst_pipeline_new("output-pipeline"));
        if (!data.output_pipeline.get()) {
            sf::error(output_pipeline, "Output pipeline could not be created. gst_pipeline_new returned nullptr.");
            return false;
        }

        return create_sei_source(data);
    }

    /**
     * @brief Configures output paths for file branches.
     * @param data Global data.
     * @param elements Output pipeline elements structure.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool configure_paths(global_data &data, output_pipeline_elements &elements) {
        return configure_output_paths(data.params, elements);
    }

    /**
     * @brief Sets up probes and heartbeat if needed.
     * @param data Global data.
     * @param success Whether pipeline creation succeeded.
     * @param rtp_port RTP port.
     */
    void setup_probes_and_heartbeat(global_data &data, bool success, uint16_t rtp_port) {
        if (success) {
            // The facade is non-owning; probe callbacks capture the aggregate via instance().
            output_branch_facade facade(data);
            if (!setup_output_pipeline_probes(facade)) {
                sf::warning(output_pipeline, "Failed to setup output pipeline probes, but pipeline creation succeeded");
            }
        }

        // Start heartbeat thread if RTP streaming is enabled and pipeline creation succeeded
        if (success && rtp_port > 0) {
            if (!start_rtp_heartbeat(data)) {
                sf::warning(output_pipeline, "Failed to start RTP heartbeat thread, but continuing");
            }
        }
    }
} // namespace

/**
 * @brief Builds and configures the output pipeline.
 * @param data Global data structure to populate with elements.
 * @return True if the pipeline was created successfully.
 */
bool create_output_pipeline(global_data &data) {
    if (!create_pipeline_and_sei_source(data)) {
        return false;
    }

    output_pipeline_elements elements;

    // Create tee element.
    // allow-not-linked=true prevents the tee from returning GST_FLOW_NOT_LINKED
    // when a branch (e.g. the debug decode branch) has not yet negotiated caps.
    // Without this, a slow-to-negotiate branch blocks data flow to all branches,
    // deadlocking the entire output pipeline.
    elements.tee = gst_element_factory_make("tee", "video-tee");
    if (!elements.tee) {
        sf::error(output_pipeline, "Failed to create tee element for output pipeline");
        return false;
    }
    g_object_set(elements.tee, "allow-not-linked", TRUE, nullptr);

    output_branch_controller branch_controller(data);
    if (!branch_controller.initialize_enabled_branches()) {
        return false;
    }

    // Check if any branches are enabled
    if (!branch_controller.has_enabled_branches()) {
        sf::warning(output_pipeline,
                    "No output branches are enabled. Adding fakesink to prevent pipeline linking issues.");
        return branch_controller.attach_fakesink_fallback();
    }

    // Create elements for all enabled branches
    if (!branch_controller.create_branch_elements(elements)) {
        return false;
    }

    // Store h265_filesink reference for backward compatibility
    data.h265_filesink = elements.sink_file;

    // Get streaming port (stored by UDP/WebRTC branches during create_elements)
    const uint16_t streaming_port = data.assigned_rtp_port.load();

    // Configure paths for file branches
    if (!configure_paths(data, elements)) {
        return false;
    }

    // Configure elements for all enabled branches
    if (!branch_controller.configure_branch_elements(elements)) {
        return false;
    }

    // Setup signals for branches that need it (e.g., WebRTC)
    if (!branch_controller.setup_branch_signals(elements)) {
        return false;
    }

    // Add all branches to pipeline
    gst_bin_add_many(GST_BIN(data.output_pipeline.get()), data.encoded_ports.sei_src, elements.tee, nullptr);
    if (!branch_controller.add_branches_to_pipeline(elements)) {
        return false;
    }

    // Link sei_src to tee
    if (!gst_element_link(data.encoded_ports.sei_src, elements.tee)) {
        sf::error(output_pipeline, "Could not link sei_src to tee");
        return false;
    }

    // Link all branches
    if (!branch_controller.link_branch_chains(elements)) {
        return false;
    }
    if (!branch_controller.link_branches_to_tee(elements.tee, elements)) {
        return false;
    }

    // Setup probes and heartbeat
    setup_probes_and_heartbeat(data, true, streaming_port);

    branch_controller.report_pipeline_created();
    branch_controller.cleanup();

    return true;
}
