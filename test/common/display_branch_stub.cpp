// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../encoder_cli/src/gst/output_pipeline/output_branches/display/display_branch.h"

// ponytail: test-only stand-in when DeepStream headers are absent. Real
// display_branch.cpp includes nvds metadata APIs; upgrade by compiling the
// production file once DEEPSTREAM_PATH is available.
output_branch_functions create_display_branch_functions() {
    return output_branch_functions{
            .is_enabled = [](const output_branch_facade &) { return false; },
            .create_elements = [](output_branch_facade &, output_pipeline_elements &) { return true; },
            .configure_elements = [](const output_branch_facade &, output_pipeline_elements &) { return true; },
            .link_branch = [](const output_pipeline_elements &) { return true; },
            .link_to_tee = [](GstElement *, const output_pipeline_elements &) { return true; },
            .add_to_pipeline = [](const output_branch_facade &, const output_pipeline_elements &) {},
            .get_branch_name = []() { return "display"; },
            .setup_signals = [](const output_branch_facade &, const output_pipeline_elements &) { return true; },
    };
}

output_branch_context create_display_branch_context() {
    return output_branch_context{.functions = create_display_branch_functions()};
}
