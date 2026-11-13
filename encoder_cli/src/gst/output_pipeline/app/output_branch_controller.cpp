// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "output_branch_controller.h"

#include "../../../../libvcai/include/logging.h"
#include "../../../bridge/app/pipeline_app_adapter.h"
#include "../../../global_data.h"
#include "../output_pipeline_debug.h"

namespace {
    [[nodiscard]] const char *resolve_branch_name(const output_branch_context &branch) {
        if (!branch.functions.get_branch_name) {
            return "unknown";
        }
        const char *const name = branch.functions.get_branch_name();
        return (name != nullptr && name[0] != '\0') ? name : "unknown";
    }
} // namespace

bool output_branch_controller::initialize_enabled_branches() {
    all_branch_contexts = create_output_branch_contexts();
    enabled_branches = filter_enabled_branches(facade_, all_branch_contexts);

    sf::info(output_pipeline, "Output pipeline branches: %zu enabled out of %zu total", enabled_branches.size(),
             all_branch_contexts.size());
    for (const auto &branch: enabled_branches) {
        sf::info(output_pipeline, "  Enabled branch: %s", resolve_branch_name(branch));
    }
    return true;
}

bool output_branch_controller::create_branch_elements(output_pipeline_elements &elements) const {
    // ponytail: create_elements mutates assigned_rtp_port, so pass a non-const facade built
    // from the retained aggregate (data stays a non-const reference member, as before).
    output_branch_facade facade(data);
    for (const auto &branch: enabled_branches) {
        const char *const branch_name = resolve_branch_name(branch);
        if (!branch.functions.create_elements) {
            sf::error(output_pipeline, "Output branch controller contract error: missing create_elements for %s",
                      branch_name);
            return false;
        }
        if (!branch.functions.create_elements(facade, elements)) {
            sf::error(output_pipeline, "Failed to create elements for branch: %s", branch_name);
            return false;
        }
    }
    return true;
}

bool output_branch_controller::configure_branch_elements(output_pipeline_elements &elements) const {
    for (const auto &branch: enabled_branches) {
        const char *const branch_name = resolve_branch_name(branch);
        if (!branch.functions.configure_elements) {
            sf::error(output_pipeline, "Output branch controller contract error: missing configure_elements for %s",
                      branch_name);
            return false;
        }
        if (!branch.functions.configure_elements(facade_, elements)) {
            sf::error(output_pipeline, "Failed to configure elements for branch: %s", branch_name);
            return false;
        }
    }
    return true;
}

bool output_branch_controller::setup_branch_signals(const output_pipeline_elements &elements) const {
    for (const auto &branch: enabled_branches) {
        const char *const branch_name = resolve_branch_name(branch);
        if (!branch.functions.setup_signals) {
            sf::error(output_pipeline, "Output branch controller contract error: missing setup_signals for %s",
                      branch_name);
            return false;
        }
        if (!branch.functions.setup_signals(facade_, elements)) {
            sf::error(output_pipeline, "Failed to setup signals for branch: %s", branch_name);
            return false;
        }
    }
    return true;
}

bool output_branch_controller::add_branches_to_pipeline(const output_pipeline_elements &elements) const {
    for (const auto &branch: enabled_branches) {
        const char *const branch_name = resolve_branch_name(branch);
        if (!branch.functions.add_to_pipeline) {
            sf::error(output_pipeline, "Output branch controller contract error: missing add_to_pipeline for %s",
                      branch_name);
            return false;
        }
        branch.functions.add_to_pipeline(facade_, elements);
    }
    return true;
}

bool output_branch_controller::link_branch_chains(const output_pipeline_elements &elements) const {
    for (const auto &branch: enabled_branches) {
        const char *const branch_name = resolve_branch_name(branch);
        if (!branch.functions.link_branch) {
            sf::error(output_pipeline, "Output branch controller contract error: missing link_branch for %s",
                      branch_name);
            return false;
        }
        sf::info(output_pipeline, "Linking branch: %s ...", branch_name);
        if (!branch.functions.link_branch(elements)) {
            sf::error(output_pipeline, "Failed to link branch: %s", branch_name);
            return false;
        }
    }
    return true;
}

bool output_branch_controller::link_branches_to_tee(GstElement *tee_element,
                                                    const output_pipeline_elements &elements) const {
    if (tee_element == nullptr) {
        sf::error(output_pipeline, "Output branch controller received null tee element");
        return false;
    }

    for (const auto &branch: enabled_branches) {
        const char *const branch_name = resolve_branch_name(branch);
        if (!branch.functions.link_to_tee) {
            sf::error(output_pipeline, "Output branch controller contract error: missing link_to_tee for %s",
                      branch_name);
            return false;
        }
        if (!branch.functions.link_to_tee(tee_element, elements)) {
            sf::error(output_pipeline, "Failed to link tee to branch: %s", branch_name);
            return false;
        }
        sf::info(output_pipeline, "Branch linked successfully: %s", branch_name);
    }
    return true;
}

bool output_branch_controller::attach_fakesink_fallback() const {
    if (data.output_pipeline == nullptr || data.encoded_ports.sei_src == nullptr) {
        sf::error(output_pipeline, "Cannot attach fallback fakesink: output pipeline or sei_src is null");
        return false;
    }

    GstElement *fakesink = gst_element_factory_make("fakesink", "output_fakesink");
    if (!fakesink) {
        sf::error(output_pipeline, "Could not create fallback fakesink element");
        return false;
    }

    gst_bin_add(GST_BIN(data.output_pipeline.get()), fakesink);
    if (!gst_element_link(data.encoded_ports.sei_src, fakesink)) {
        sf::error(output_pipeline, "Could not link sei_src to fallback fakesink");
        return false;
    }

    pipeline_app_adapter(data).report_output_pipeline_status("created successfully (fakesink only)");
    return true;
}

void output_branch_controller::report_pipeline_created() const {
    pipeline_app_adapter(data).report_output_pipeline_status("created successfully");
}

void output_branch_controller::cleanup() {
    enabled_branches.clear();
    all_branch_contexts.clear();
}
