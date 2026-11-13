// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <functional>
#include <gst/gst.h>

/**
 * @brief Forward declaration for the narrow output branch state view.
 */
class output_branch_facade;
/**
 * @brief Forward declaration for output pipeline element container.
 */
struct output_pipeline_elements;

/**
 * @file branch_interface.height
 * @brief Interface for output branch implementations using std::function.
 */

/**
 * @brief Function table for output branch implementations using std::function.
 *
 * IMPORTANT: This uses std::function, NOT C-style function pointers.
 *
 * Benefits of std::function over function pointers:
 * - Lambdas with captures (eliminates need for void* user_data)
 * - Function objects and std::bind
 * - Better type safety and readability
 * - No void* user_data parameter needed
 *
 * Example usage:
 *   output_branch_functions funcs;
 *   funcs.is_enabled = [](const output_branch_facade &d) { return d.params.debug.enable_display_branch; };
 *   funcs.create_elements = [](output_branch_facade &d, output_pipeline_elements &e) { return true; };
 */
struct output_branch_functions {
    /**
     * @brief Check if this branch should be enabled.
     * @param data The narrow output branch state view.
     * @return true if branch should be enabled, false otherwise.
     *
     * Note: This is std::function<bool(const output_branch_facade &)>, NOT a function pointer.
     */
    std::function<bool(const output_branch_facade &)> is_enabled;

    /**
     * @brief Create GStreamer elements for this branch.
     * @param data The narrow output branch state view (non-const to allow UDP/WebRTC branches to store assigned port).
     * @param elements Output pipeline elements structure to populate.
     * @return true on success, false on failure.
     *
     * Note: UDP and WebRTC branches will store assigned port in data.assigned_rtp_port during this call.
     */
    std::function<bool(output_branch_facade &, output_pipeline_elements &)> create_elements;

    /**
     * @brief Configure GStreamer elements for this branch.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    std::function<bool(const output_branch_facade &, output_pipeline_elements &)> configure_elements;

    /**
     * @brief Link branch elements together.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    std::function<bool(const output_pipeline_elements &)> link_branch;

    /**
     * @brief Link branch to tee element.
     * @param tee The tee element.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     */
    std::function<bool(GstElement *, const output_pipeline_elements &)> link_to_tee;

    /**
     * @brief Add branch elements to pipeline.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     */
    std::function<void(const output_branch_facade &, const output_pipeline_elements &)> add_to_pipeline;

    /**
     * @brief Get the name of this branch for logging.
     * @return Branch name string (e.g., "display", "file-h265", "file-mp4", "debug", "udp", "webrtc").
     */
    std::function<const char *()> get_branch_name;

    /**
     * @brief Optional: Setup additional signals/connections after element creation and configuration.
     * This is used for WebRTC branch which needs signaling client and signal connections.
     * @param data The narrow output branch state view.
     * @param elements Output pipeline elements structure.
     * @return true on success, false on failure.
     *
     * Note: Most branches will return a no-op function that always returns true.
     * Only WebRTC branch will implement actual signal setup logic.
     */
    std::function<bool(const output_branch_facade &, const output_pipeline_elements &)> setup_signals;
};

/**
 * @brief Output branch context containing function table.
 *
 * No user_data needed - lambdas can capture any needed state.
 */
struct output_branch_context {
    output_branch_functions functions;
};
