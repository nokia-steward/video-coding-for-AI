#pragma once

#include <vector>
#include "branch_interface.h"

struct rtp_pipeline;

/**
 * @brief Create all output branch contexts.
 * @param pipeline The pipeline (for checking which branches are enabled).
 * @return Vector of enabled branch contexts.
 */
std::vector<output_branch_context> create_output_branch_contexts(const rtp_pipeline *pipeline);
