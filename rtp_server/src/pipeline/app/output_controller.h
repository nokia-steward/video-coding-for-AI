// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <vector>

#include "../output/branch_interface.h"

struct rtp_pipeline;

/**
 * @brief Orchestrates output branch lifecycle independently of media core logic.
 */
struct output_branch_controller {
    output_branch_controller() : branch_contexts() {}

    [[nodiscard]] bool initialize_enabled_branches(const rtp_pipeline *pipeline);
    [[nodiscard]] bool create_branch_elements(rtp_pipeline &pipeline);
    [[nodiscard]] bool configure_branch_elements(rtp_pipeline &pipeline);
    [[nodiscard]] bool add_branches_to_pipeline(rtp_pipeline &pipeline, GstElement *output_pipeline);
    [[nodiscard]] bool link_branch_chains(rtp_pipeline &pipeline);
    [[nodiscard]] bool link_branches_to_tee(rtp_pipeline &pipeline, GstElement *tee_element);
    void cleanup(rtp_pipeline &pipeline);

private:
    std::vector<output_branch_context> branch_contexts;
};
