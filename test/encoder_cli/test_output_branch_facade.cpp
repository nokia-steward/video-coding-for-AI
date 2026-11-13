// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <string>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/branch_factory.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/output_branch_facade.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_output_branch_facade.cpp
 * @brief Unit tests for the output branch narrow view interface.
 *
 * Exercises output_branch_facade as the seam branch selection goes through,
 * without embedding the aggregate's unrelated state (per the PRD testing
 * decision). The facade is a non-owning view over global_data, so it is
 * constructed over a minimal global_data and only the output slice is touched.
 */

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 1920;
        config.height = 1080;
        config.framerate = 30;
        config.debug.enable_h265_branch = false;
        config.debug.enable_mp4_branch = false;
        config.debug.enable_display_branch = false;
        config.debug.save_frames = false;
        return config;
    }
} // namespace

class OutputBranchFacadeTest : public GStreamerTestFixture {};

TEST_F(OutputBranchFacadeTest, FacadeReflectsParamsAndSelectsDisplayBranch) {
    encoder_config config = make_base_config();
    config.debug.enable_display_branch = true;
    global_data data(std::move(config));

    const output_branch_facade facade(data);
    EXPECT_EQ(facade.params.debug.enable_display_branch, true);
    EXPECT_EQ(facade.instance().output_display_runtime.border_dump_initialized, false);
}

TEST_F(OutputBranchFacadeTest, FilterSelectsExactlyDisplayWhenOnlyDisplayEnabled) {
    encoder_config config = make_base_config();
    config.debug.enable_display_branch = true;
    global_data data(std::move(config));
    output_branch_facade facade(data);

    const auto enabled = filter_enabled_branches(facade, create_output_branch_contexts());

    ASSERT_EQ(enabled.size(), 1u);
    EXPECT_EQ(std::string(enabled.front().functions.get_branch_name()), "display");
}

TEST_F(OutputBranchFacadeTest, FilterSelectsEmptyWhenNothingEnabled) {
    encoder_config config = make_base_config();
    global_data data(std::move(config));
    output_branch_facade facade(data);

    const auto enabled = filter_enabled_branches(facade, create_output_branch_contexts());
    EXPECT_TRUE(enabled.empty());
}

TEST_F(OutputBranchFacadeTest, FilterSelectsMultipleBranches) {
    encoder_config config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.save_frames = true;
    global_data data(std::move(config));
    output_branch_facade facade(data);

    const auto enabled = filter_enabled_branches(facade, create_output_branch_contexts());

    ASSERT_EQ(enabled.size(), 2u);
    EXPECT_EQ(std::string(enabled[0].functions.get_branch_name()), "display");
    EXPECT_EQ(std::string(enabled[1].functions.get_branch_name()), "debug");
}

TEST_F(OutputBranchFacadeTest, FacadeBindsMutableAssignedPortToAggregate) {
    encoder_config config = make_base_config();
    global_data data(std::move(config));
    output_branch_facade facade(data);

    facade.assigned_rtp_port.store(5004);
    facade.assigned_rtp_session_id.store(42);
    EXPECT_EQ(data.assigned_rtp_port.load(), 5004);
    EXPECT_EQ(data.assigned_rtp_session_id.load(), 42);
}
