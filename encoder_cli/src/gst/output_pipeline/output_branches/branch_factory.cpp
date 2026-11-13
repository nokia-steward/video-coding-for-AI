// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "branch_factory.h"
#include "../../../../libvcai/include/logging.h"
#include "../output_pipeline_debug.h"
#include "debug/debug_branch.h"
#include "display/display_branch.h"
#include "file_h265/file_h265_branch.h"
#include "file_mp4/file_mp4_branch.h"
#include "output_branch_facade.h"
#include "udp/udp_branch.h"
#include "webrtc/webrtc_branch.h"

#include <vector>

/**
 * @file branch_factory.cpp
 * @brief Implementation of output branch factory functions.
 */

/**
 * @brief Creates all output branch contexts.
 *
 * Creates contexts for all available output branches: display, H.265 file,
 * MP4 file, debug, UDP, and WebRTC.
 *
 * @return Vector of output branch contexts.
 */
std::vector<output_branch_context> create_output_branch_contexts() {
    std::vector<output_branch_context> contexts;

    contexts.push_back(create_display_branch_context());
    contexts.push_back(create_file_h265_branch_context());
    contexts.push_back(create_file_mp4_branch_context());
    contexts.push_back(create_debug_branch_context());
    contexts.push_back(create_udp_branch_context());
    contexts.push_back(create_webrtc_branch_context());

    return contexts;
}

std::vector<output_branch_context> filter_enabled_branches(const output_branch_facade &data,
                                                           const std::vector<output_branch_context> &contexts) {

    std::vector<output_branch_context> enabled_branches;

    for (const auto &context: contexts) {
        if (context.functions.is_enabled(data)) {
            sf::info(output_pipeline, "Output branch enabled: %s", context.functions.get_branch_name());
            enabled_branches.push_back(context);
        }
    }

    if (enabled_branches.empty()) {
        sf::warning(output_pipeline, "No output branches are enabled");
    }

    return enabled_branches;
}
