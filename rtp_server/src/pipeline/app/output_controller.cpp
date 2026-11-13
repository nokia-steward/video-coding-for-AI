// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "output_controller.h"

#include "../output/branch_factory.h"
#include "../pipeline.h"
#include "logging.h"

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

namespace {
    [[nodiscard]] const char *resolve_branch_name(const output_branch_context &branch) {
        if (!branch.functions.get_branch_name) {
            return "unknown";
        }

        const char *branch_name = branch.functions.get_branch_name();
        if (!branch_name || branch_name[0] == '\0') {
            return "unknown";
        }
        return branch_name;
    }
} // namespace

bool output_branch_controller::initialize_enabled_branches(const rtp_pipeline *pipeline) {
    if (!pipeline) {
        sf::error(pipeline_category, "Output branch controller received null pipeline");
        return false;
    }

    branch_contexts = create_output_branch_contexts(pipeline);
    return true;
}

bool output_branch_controller::create_branch_elements(rtp_pipeline &pipeline) {
    for (auto &branch: branch_contexts) {
        const char *branch_name = resolve_branch_name(branch);
        if (!branch.functions.create_elements) {
            sf::error(pipeline_category, "Output branch controller contract error: missing create_elements for %s",
                      branch_name);
            return false;
        }

        if (!branch.functions.create_elements(&pipeline)) {
            sf::error(pipeline_category, "Failed to create %s branch elements for port %u", branch_name,
                      pipeline.get_port());
            return false;
        }
    }
    return true;
}

bool output_branch_controller::configure_branch_elements(rtp_pipeline &pipeline) {
    for (auto &branch: branch_contexts) {
        const char *branch_name = resolve_branch_name(branch);
        if (!branch.functions.configure_elements) {
            sf::error(pipeline_category, "Output branch controller contract error: missing configure_elements for %s",
                      branch_name);
            return false;
        }

        if (!branch.functions.configure_elements(&pipeline)) {
            sf::error(pipeline_category, "Failed to configure %s branch for port %u", branch_name, pipeline.get_port());
            return false;
        }
    }
    return true;
}

bool output_branch_controller::add_branches_to_pipeline(rtp_pipeline &pipeline, GstElement *output_pipeline) {
    if (!output_pipeline) {
        sf::error(pipeline_category, "Output branch controller received null output pipeline for port %u",
                  pipeline.get_port());
        return false;
    }

    for (auto &branch: branch_contexts) {
        const char *branch_name = resolve_branch_name(branch);
        if (!branch.functions.add_to_pipeline) {
            sf::error(pipeline_category, "Output branch controller contract error: missing add_to_pipeline for %s",
                      branch_name);
            return false;
        }

        if (!branch.functions.add_to_pipeline(&pipeline, output_pipeline)) {
            sf::error(pipeline_category, "Failed to add %s branch to pipeline for port %u", branch_name,
                      pipeline.get_port());
            return false;
        }
    }
    return true;
}

bool output_branch_controller::link_branch_chains(rtp_pipeline &pipeline) {
    for (auto &branch: branch_contexts) {
        const char *branch_name = resolve_branch_name(branch);
        if (!branch.functions.link_branch) {
            sf::error(pipeline_category, "Output branch controller contract error: missing link_branch for %s",
                      branch_name);
            return false;
        }

        if (!branch.functions.link_branch(&pipeline)) {
            sf::error(pipeline_category, "Failed to link %s branch for port %u", branch_name, pipeline.get_port());
            return false;
        }
    }
    return true;
}

bool output_branch_controller::link_branches_to_tee(rtp_pipeline &pipeline, GstElement *tee_element) {
    if (!tee_element) {
        sf::error(pipeline_category, "Output branch controller received null tee element for port %u",
                  pipeline.get_port());
        return false;
    }

    for (auto &branch: branch_contexts) {
        const char *branch_name = resolve_branch_name(branch);
        if (!branch.functions.link_to_tee) {
            sf::error(pipeline_category, "Output branch controller contract error: missing link_to_tee for %s",
                      branch_name);
            return false;
        }

        if (!branch.functions.link_to_tee(&pipeline, tee_element)) {
            sf::error(pipeline_category, "Failed to link %s branch to tee for port %u", branch_name,
                      pipeline.get_port());
            return false;
        }
    }
    return true;
}

void output_branch_controller::cleanup(rtp_pipeline &pipeline) {
    for (auto &branch: branch_contexts) {
        if (branch.functions.cleanup) {
            branch.functions.cleanup(&pipeline);
        }
    }
    branch_contexts.clear();
}
