#pragma once

#include <functional>
#include <gst/gst.h>
#include <optional>
#include <string>

// Forward declaration
struct rtp_pipeline;

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
 *   funcs.is_enabled = [](const rtp_pipeline *p) { return true; };
 */
struct output_branch_functions {
    /**
     * @brief Check if this branch should be enabled.
     * @param pipeline The pipeline.
     * @return true if branch should be enabled, false otherwise.
     *
     * Note: This is std::function<bool(const rtp_pipeline *)>, NOT a function pointer.
     */
    std::function<bool(const rtp_pipeline *)> is_enabled;

    /**
     * @brief Create branch-specific elements.
     * @param pipeline The pipeline to populate.
     * @return true if successful, false otherwise.
     */
    std::function<bool(rtp_pipeline *)> create_elements;

    /**
     * @brief Configure branch-specific elements.
     * @param pipeline The pipeline to configure.
     * @return true if successful, false otherwise.
     */
    std::function<bool(rtp_pipeline *)> configure_elements;

    /**
     * @brief Link branch elements together.
     * @param pipeline The pipeline to link.
     * @return true if successful, false otherwise.
     */
    std::function<bool(rtp_pipeline *)> link_branch;

    /**
     * @brief Link branch to tee element.
     * @param pipeline The pipeline.
     * @param tee_element The tee element to link to.
     * @return true if successful, false otherwise.
     */
    std::function<bool(rtp_pipeline *, GstElement *)> link_to_tee;

    /**
     * @brief Add branch elements to pipeline bin.
     * @param pipeline The pipeline.
     * @param output_pipeline The output pipeline bin.
     * @return true if successful, false otherwise.
     */
    std::function<bool(rtp_pipeline *, GstElement *)> add_to_pipeline;

    /**
     * @brief Cleanup branch-specific resources.
     * @param pipeline The pipeline.
     */
    std::function<void(rtp_pipeline *)> cleanup;

    /**
     * @brief Get branch name for logging.
     * @return Branch name string.
     */
    std::function<const char *()> get_branch_name;
};

/**
 * @brief Output branch context containing function table.
 *
 * No user_data needed - lambdas can capture any needed state.
 */
struct output_branch_context {
    output_branch_functions functions;
};
