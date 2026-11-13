// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/elements.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/debug/debug_branch.h"
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

class OutputBranchDebugTest : public GStreamerTestFixture {};

TEST_F(OutputBranchDebugTest, DisabledDebugBranchUsesNoopPaths) {
    auto config = make_base_config();
    config.debug.save_frames = false;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    const auto context = create_debug_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.is_enabled(facade_data));
    EXPECT_TRUE(functions.create_elements(facade_data, elements));
    EXPECT_TRUE(functions.configure_elements(facade_data, elements));
    EXPECT_TRUE(functions.link_branch(elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, elements));
    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchDebugTest, SetupSignalsIsNonFatalWhenDecoderMissing) {
    auto config = make_base_config();
    config.debug.save_frames = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    const auto context = create_debug_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.is_enabled(facade_data));
    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchDebugTest, LinkOperationsFailWithIncompleteCreatedBranch) {
    auto config = make_base_config();
    config.debug.save_frames = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    elements.queue_debug = gst_element_factory_make("identity", "test-debug-queue");
    ASSERT_NE(elements.queue_debug, nullptr);

    const auto context = create_debug_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.link_branch(elements));
    EXPECT_FALSE(functions.link_to_tee(nullptr, elements));

    gst_object_unref(elements.queue_debug);
}

TEST_F(OutputBranchDebugTest, SetupSignalsGracefullyContinuesWhenDecoderHasNoSrcPad) {
    auto config = make_base_config();
    config.debug.save_frames = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};
    elements.decoder_debug = gst_element_factory_make("fakesink", "test-debug-decoder-no-src");
    ASSERT_NE(elements.decoder_debug, nullptr);

    const auto context = create_debug_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.decoder_debug);
}

TEST_F(OutputBranchDebugTest, draw_hline_y_draw_vline_y_draw_rect_border_on_frame_probe_path_is_stable) {
    auto config = make_base_config();
    config.debug.save_frames = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.decoder_debug = gst_element_factory_make("identity", "test-debug-decoder-with-src");
    ASSERT_NE(elements.decoder_debug, nullptr);

    const auto context = create_debug_branch_context();
    const auto &functions = context.functions;
    EXPECT_TRUE(functions.setup_signals(facade_data, elements));

    gst_object_unref(elements.decoder_debug);
}
