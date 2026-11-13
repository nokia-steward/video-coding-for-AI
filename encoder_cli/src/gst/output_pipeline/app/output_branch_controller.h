// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../elements.h"
#include "../output_branches/branch_factory.h"
#include "../output_branches/output_branch_facade.h"

#include <gst/gst.h>
#include <vector>

/**
 * @file output_branch_controller.h
 * @brief Orchestrates output branch lifecycle for output pipeline setup.
 */

/**
 * @brief App-side controller for output branch initialization and linking order.
 */
class output_branch_controller {
public:
    /**
     * @brief Creates a controller bound to one output pipeline runtime.
     * @param data_ref Shared runtime state.
     */
    explicit output_branch_controller(global_data &data_ref) :
        data(data_ref), facade_(data_ref), all_branch_contexts(), enabled_branches() {}

    /**
     * @brief Initializes and filters enabled output branches.
     * @return True on success.
     */
    [[nodiscard]] bool initialize_enabled_branches();

    /**
     * @brief Returns true when any output branch is enabled.
     * @return True when at least one branch is enabled.
     */
    [[nodiscard]] bool has_enabled_branches() const { return !enabled_branches.empty(); }

    /**
     * @brief Returns number of enabled branches.
     * @return Enabled branch count.
     */
    [[nodiscard]] size_t enabled_branch_count() const { return enabled_branches.size(); }

    /**
     * @brief Creates elements for enabled branches.
     * @param elements Output elements holder.
     * @return True on success.
     */
    [[nodiscard]] bool create_branch_elements(output_pipeline_elements &elements) const;

    /**
     * @brief Configures elements for enabled branches.
     * @param elements Output elements holder.
     * @return True on success.
     */
    [[nodiscard]] bool configure_branch_elements(output_pipeline_elements &elements) const;

    /**
     * @brief Sets up optional signals for enabled branches.
     * @param elements Output elements holder.
     * @return True on success.
     */
    [[nodiscard]] bool setup_branch_signals(const output_pipeline_elements &elements) const;

    /**
     * @brief Adds enabled branches to output pipeline bin.
     * @param elements Output elements holder.
     * @return True on success.
     */
    [[nodiscard]] bool add_branches_to_pipeline(const output_pipeline_elements &elements) const;

    /**
     * @brief Links each enabled branch internal chain.
     * @param elements Output elements holder.
     * @return True on success.
     */
    [[nodiscard]] bool link_branch_chains(const output_pipeline_elements &elements) const;

    /**
     * @brief Links tee element to enabled branch queues.
     * @param tee_element Tee element.
     * @param elements Output elements holder.
     * @return True on success.
     */
    [[nodiscard]] bool link_branches_to_tee(GstElement *tee_element, const output_pipeline_elements &elements) const;

    /**
     * @brief Builds fallback fakesink path when no branches are enabled.
     * @return True on success.
     */
    [[nodiscard]] bool attach_fakesink_fallback() const;

    /**
     * @brief Reports successful output pipeline creation via app adapter.
     */
    void report_pipeline_created() const;

    /**
     * @brief Clears controller-managed branch state.
     */
    void cleanup();

private:
    /// Raw aggregate kept for the orchestration bits (fallback fakesink, pipeline status report).
    /// ponytail: branch selection/instantiation goes through facade_; this raw ref remains for
    /// attach_fakesink_fallback/report_pipeline_created until slice 07 migrates orchestration.
    global_data &data;
    output_branch_facade facade_;
    std::vector<output_branch_context> all_branch_contexts;
    std::vector<output_branch_context> enabled_branches;
};
