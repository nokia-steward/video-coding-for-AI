// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/elements.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/display/display_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/output_branch_facade.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }
} // namespace

class OutputBranchDisplayTest : public GStreamerTestFixture {};

TEST_F(OutputBranchDisplayTest, DisabledDisplayBranchUsesNoopPaths) {
    auto config = make_base_config();
    config.debug.enable_display_branch = false;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.is_enabled(facade_data));
    EXPECT_TRUE(functions.create_elements(facade_data, elements));
    EXPECT_TRUE(functions.configure_elements(facade_data, elements));
    EXPECT_TRUE(functions.link_branch(elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, elements));
    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchDisplayTest, BranchNameIsDisplay) {
    const auto context = create_display_branch_context();
    EXPECT_STREQ(context.functions.get_branch_name(), "display");
}

TEST_F(OutputBranchDisplayTest, EnabledDisplayBranchWithNoOverlaysSkipsSignalSetupWork) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_borders = false;
    config.debug.draw_fps = false;
    config.debug.draw_bitrate = false;
    config.debug.draw_bitrate_large = false;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.is_enabled(facade_data));
    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchDisplayTest, EnabledDisplayBranchWithOverlaysRequiresProbeElement) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_fps = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.is_enabled(facade_data));
    EXPECT_FALSE(functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchDisplayTest, EnabledDisplayBranchWithOverlaysSucceedsWithFallbackProbeElement) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_fps = true;
    config.debug.draw_borders = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.nvosd_display = gst_element_factory_make("identity", "test-display-probe");
    ASSERT_NE(elements.nvosd_display, nullptr);

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.nvosd_display);
}

TEST_F(OutputBranchDisplayTest, LinkAndTeeLinkFailWhenBranchMarkedCreatedButElementsMissing) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.sink_display = gst_element_factory_make("fakesink", "test-display-sink");
    ASSERT_NE(elements.sink_display, nullptr);

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.link_branch(elements));
    EXPECT_FALSE(functions.link_to_tee(nullptr, elements));

    gst_object_unref(elements.sink_display);
}

TEST_F(OutputBranchDisplayTest, SetupSignalsFailsWhenProbeElementPadIsUnavailable) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_fps = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.streammux_display = gst_element_factory_make("fakesink", "test-streammux-no-src-pad");
    ASSERT_NE(elements.streammux_display, nullptr);

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.streammux_display);
}

TEST_F(OutputBranchDisplayTest, SetupSignalsSucceedsWhenStreammuxProbePadExists) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_fps = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.streammux_display = gst_element_factory_make("identity", "test-streammux-with-src-pad");
    ASSERT_NE(elements.streammux_display, nullptr);

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.streammux_display);
}

TEST_F(OutputBranchDisplayTest, EnabledDisplayBranchWithBitrateOverlaySucceedsWithStreammuxProbePad) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_bitrate_large = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.streammux_display = gst_element_factory_make("identity", "test-streammux-bitrate-src-pad");
    ASSERT_NE(elements.streammux_display, nullptr);

    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.streammux_display);
}

TEST_F(OutputBranchDisplayTest, get_display_overlay_flags_draw_bitrate_requires_probe_element) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_bitrate = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    const auto context = create_display_branch_context();
    EXPECT_TRUE(context.functions.is_enabled(facade_data));
    EXPECT_FALSE(context.functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchDisplayTest, get_display_overlay_flags_draw_bitrate_large_requires_probe_element) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_bitrate_large = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    const auto context = create_display_branch_context();
    EXPECT_TRUE(context.functions.is_enabled(facade_data));
    EXPECT_FALSE(context.functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchDisplayTest, ensure_display_border_dump_stream_and_dump_display_border_rectangles_probe_attaches) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_borders = true;
    config.packer_logging = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.streammux_display = gst_element_factory_make("identity", "test-streammux-borders-src-pad");
    ASSERT_NE(elements.streammux_display, nullptr);

    const auto context = create_display_branch_context();
    EXPECT_TRUE(context.functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.streammux_display);
}

TEST_F(OutputBranchDisplayTest, format_fps_string_overlay_path_is_reached_when_display_fps_probe_is_enabled) {
    auto config = make_base_config();
    config.debug.enable_display_branch = true;
    config.debug.draw_fps = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.streammux_display = gst_element_factory_make("identity", "display-fps-format-path");
    ASSERT_NE(elements.streammux_display, nullptr);

    const auto context = create_display_branch_context();
    EXPECT_TRUE(context.functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.streammux_display);
}
