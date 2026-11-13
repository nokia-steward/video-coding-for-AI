// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "output_branches/output_branch_facade.h"

/**
 * @file probes.height
 * @brief Forward declaration for output pipeline pad probe setup.
 * @note Forward declaration for internal use.
 */

/**
 * @brief Sets up pad probes on the output pipeline to track frames.
 * @param data The narrow output branch state view.
 * @return true if probes were successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_output_pipeline_probes(output_branch_facade &data);
