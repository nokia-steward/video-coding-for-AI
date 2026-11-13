#include "branch_factory.h"
#include "../pipeline.h"
#include "debug_frames/debug_frames_branch.h"
#include "display/display_branch.h"
#include "file/file_branch.h"
#include "logging.h"
#include "rtp_output/rtp_output_branch.h"

// Debug category for output branch factory operations.
GST_DEBUG_CATEGORY_STATIC(branch_factory_category);
#define GST_CAT_DEFAULT branch_factory_category

std::vector<output_branch_context> create_output_branch_contexts(const rtp_pipeline *pipeline) {
    GST_DEBUG_CATEGORY_INIT(branch_factory_category, "branch-factory", 0, "Output Branch Factory");

    std::vector<output_branch_context> branches;

    if (!pipeline) {
        return branches;
    }

    // Always create display branch
    auto display_ctx = create_display_branch_context();
    if (display_ctx.functions.is_enabled && display_ctx.functions.is_enabled(pipeline)) {
        branches.push_back(display_ctx);
        sf::info(branch_factory_category, "Display branch enabled for port %u", pipeline->get_port());
    }

    // Conditionally create file branch
    auto file_ctx = create_file_branch_context();
    if (file_ctx.functions.is_enabled && file_ctx.functions.is_enabled(pipeline)) {
        branches.push_back(file_ctx);
        sf::info(branch_factory_category, "File branch enabled for port %u", pipeline->get_port());
    }

    // Conditionally create RTP output branch
    auto rtp_output_ctx = create_rtp_output_branch_context();
    if (rtp_output_ctx.functions.is_enabled && rtp_output_ctx.functions.is_enabled(pipeline)) {
        branches.push_back(rtp_output_ctx);
        sf::info(branch_factory_category, "RTP output branch enabled for port %u", pipeline->get_port());
    }

    // Conditionally create debug PNG frames branch
    auto debug_frames_ctx = create_debug_frames_branch_context();
    if (debug_frames_ctx.functions.is_enabled && debug_frames_ctx.functions.is_enabled(pipeline)) {
        branches.push_back(debug_frames_ctx);
        sf::info(branch_factory_category, "Debug frames branch enabled for port %u", pipeline->get_port());
    }

    return branches;
}
