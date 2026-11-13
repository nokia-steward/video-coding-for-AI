// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/app/output_branch_controller.h"
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
        config.debug.enable_h265_branch = false;
        config.debug.enable_mp4_branch = false;
        config.debug.enable_display_branch = false;
        config.debug.save_frames = false;
        config.communication.host = "";
        config.communication.port = 0;
        return config;
    }
} // namespace

class OutputBranchControllerTest : public GStreamerTestFixture {};

TEST_F(OutputBranchControllerTest, InitializesWithoutEnabledBranchesAndNoOpLifecycleCallsSucceed) {
    auto config = make_base_config();
    global_data data(std::move(config));
    output_branch_controller controller(data);

    ASSERT_TRUE(controller.initialize_enabled_branches());
    EXPECT_FALSE(controller.has_enabled_branches());
    EXPECT_EQ(controller.enabled_branch_count(), 0U);

    output_pipeline_elements elements{};
    EXPECT_TRUE(controller.create_branch_elements(elements));
    EXPECT_TRUE(controller.configure_branch_elements(elements));
    EXPECT_TRUE(controller.setup_branch_signals(elements));
    EXPECT_TRUE(controller.add_branches_to_pipeline(elements));
    EXPECT_TRUE(controller.link_branch_chains(elements));

    GstElement *tee = gst_element_factory_make("tee", "controller-noop-tee");
    ASSERT_NE(tee, nullptr);
    EXPECT_TRUE(controller.link_branches_to_tee(tee, elements));
    gst_object_unref(tee);
}

TEST_F(OutputBranchControllerTest, FallbackPathAttachesFakesinkAndLinksFromSeiSource) {
    auto config = make_base_config();
    config.enable_status_reporter = true;
    global_data data(std::move(config));
    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("controller-fallback-output-pipeline"));
    ASSERT_NE(data.output_pipeline.get(), nullptr);
    data.encoded_ports.sei_src = gst_element_factory_make("appsrc", "controller-fallback-sei-src");
    ASSERT_NE(data.encoded_ports.sei_src, nullptr);
    gst_bin_add(GST_BIN(data.output_pipeline.get()), data.encoded_ports.sei_src);

    output_branch_controller controller(data);
    ASSERT_TRUE(controller.initialize_enabled_branches());
    ASSERT_FALSE(controller.has_enabled_branches());
    ASSERT_TRUE(controller.attach_fakesink_fallback());

    GstElement *fakesink = gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "output_fakesink");
    ASSERT_NE(fakesink, nullptr);

    GstPad *src_pad = gst_element_get_static_pad(data.encoded_ports.sei_src, "src");
    ASSERT_NE(src_pad, nullptr);
    GstPad *peer_pad = gst_pad_get_peer(src_pad);
    EXPECT_NE(peer_pad, nullptr);

    if (peer_pad != nullptr) {
        gst_object_unref(peer_pad);
    }
    gst_object_unref(src_pad);
    gst_object_unref(fakesink);
}

TEST_F(OutputBranchControllerTest, ReportsEnabledBranchCountWhenFileBranchConfigured) {
    auto config = make_base_config();
    config.debug.enable_h265_branch = true;
    global_data data(std::move(config));
    output_branch_controller controller(data);

    ASSERT_TRUE(controller.initialize_enabled_branches());
    EXPECT_GT(controller.enabled_branch_count(), 0U);
}
