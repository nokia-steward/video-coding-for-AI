// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/elements.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/display/display_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/file_h265/file_h265_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/file_mp4/file_mp4_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/output_branch_facade.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/udp/udp_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/webrtc/webrtc_branch.h"
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

    void release_if_unparented(GstElement *&element) {
        if (element == nullptr) {
            return;
        }
        GstObject *parent = gst_object_get_parent(GST_OBJECT(element));
        if (parent != nullptr) {
            gst_object_unref(parent);
            element = nullptr;
            return;
        }
        gst_object_unref(element);
        element = nullptr;
    }
} // namespace

class OutputBranchesFileNetworkTest : public GStreamerTestFixture {};

TEST_F(OutputBranchesFileNetworkTest, FileH265BranchLifecycleCoversEnableCreateConfigureAndLink) {
    const auto functions = create_file_h265_branch_functions();

    auto disabled_config = make_base_config();
    disabled_config.debug.enable_h265_branch = false;
    global_data disabled_data(std::move(disabled_config));
    output_branch_facade facade_disabled_data(disabled_data);
    output_pipeline_elements disabled_elements{};
    EXPECT_FALSE(functions.is_enabled(facade_disabled_data));
    EXPECT_TRUE(functions.create_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.configure_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.link_branch(disabled_elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, disabled_elements));
    EXPECT_TRUE(functions.setup_signals(facade_disabled_data, disabled_elements));
    EXPECT_STREQ(functions.get_branch_name(), "file-h265");

    auto enabled_config = make_base_config();
    enabled_config.debug.enable_h265_branch = true;
    enabled_config.framerate = 0;
    global_data enabled_data(std::move(enabled_config));
    output_branch_facade facade_enabled_data(enabled_data);
    output_pipeline_elements enabled_elements{};
    const bool created = functions.create_elements(facade_enabled_data, enabled_elements);
    if (!created) {
        GTEST_SKIP() << "Required file-h265 branch plugins unavailable in this environment";
    }

    ASSERT_NE(enabled_elements.queue_file, nullptr);
    ASSERT_NE(enabled_elements.sink_file, nullptr);
    ASSERT_TRUE(functions.configure_elements(facade_enabled_data, enabled_elements));

    guint max_size_buffers = 0;
    gint leaky = -1;
    g_object_get(G_OBJECT(enabled_elements.queue_file), "max-size-buffers", &max_size_buffers, "leaky", &leaky,
                 nullptr);
    EXPECT_EQ(max_size_buffers, 120U);
    EXPECT_EQ(leaky, static_cast<int>(queue_leaky_mode::no_leak));

    ASSERT_TRUE(functions.link_branch(enabled_elements));
    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "test-file-h265-tee"));
    ASSERT_NE(tee, nullptr);
    ASSERT_TRUE(functions.link_to_tee(tee.get(), enabled_elements));

    enabled_data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-file-h265-pipeline"));
    ASSERT_NE(enabled_data.output_pipeline, nullptr);
    functions.add_to_pipeline(facade_enabled_data, enabled_elements);

    sf::unique_gst_element_ptr queue_lookup(gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "q-file"));
    sf::unique_gst_element_ptr sink_lookup(gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "s-file"));
    EXPECT_NE(queue_lookup, nullptr);
    EXPECT_NE(sink_lookup, nullptr);

    release_if_unparented(enabled_elements.queue_file);
    release_if_unparented(enabled_elements.sink_file);
}

TEST_F(OutputBranchesFileNetworkTest, FileMp4BranchLifecycleCoversEnableCreateConfigureAndLink) {
    const auto functions = create_file_mp4_branch_functions();

    auto disabled_config = make_base_config();
    disabled_config.debug.enable_mp4_branch = false;
    global_data disabled_data(std::move(disabled_config));
    output_branch_facade facade_disabled_data(disabled_data);
    output_pipeline_elements disabled_elements{};
    EXPECT_FALSE(functions.is_enabled(facade_disabled_data));
    EXPECT_TRUE(functions.create_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.configure_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.link_branch(disabled_elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, disabled_elements));
    EXPECT_TRUE(functions.setup_signals(facade_disabled_data, disabled_elements));
    EXPECT_STREQ(functions.get_branch_name(), "file-mp4");

    auto enabled_config = make_base_config();
    enabled_config.debug.enable_mp4_branch = true;
    enabled_config.framerate = 0;
    global_data enabled_data(std::move(enabled_config));
    output_branch_facade facade_enabled_data(enabled_data);
    output_pipeline_elements enabled_elements{};
    const bool created = functions.create_elements(facade_enabled_data, enabled_elements);
    if (!created) {
        GTEST_SKIP() << "Required file-mp4 branch plugins unavailable in this environment";
    }

    ASSERT_NE(enabled_elements.queue_mp4, nullptr);
    ASSERT_NE(enabled_elements.parser_mp4, nullptr);
    ASSERT_NE(enabled_elements.muxer_mp4, nullptr);
    ASSERT_NE(enabled_elements.sink_mp4, nullptr);
    ASSERT_TRUE(functions.configure_elements(facade_enabled_data, enabled_elements));

    guint max_size_buffers = 0;
    gint leaky = -1;
    g_object_get(G_OBJECT(enabled_elements.queue_mp4), "max-size-buffers", &max_size_buffers, "leaky", &leaky, nullptr);
    EXPECT_EQ(max_size_buffers, 120U);
    EXPECT_EQ(leaky, static_cast<int>(queue_leaky_mode::no_leak));

    ASSERT_TRUE(functions.link_branch(enabled_elements));
    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "test-file-mp4-tee"));
    ASSERT_NE(tee, nullptr);
    ASSERT_TRUE(functions.link_to_tee(tee.get(), enabled_elements));

    enabled_data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-file-mp4-pipeline"));
    ASSERT_NE(enabled_data.output_pipeline, nullptr);
    functions.add_to_pipeline(facade_enabled_data, enabled_elements);

    sf::unique_gst_element_ptr queue_lookup(gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "q-mp4"));
    sf::unique_gst_element_ptr sink_lookup(gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "s-mp4"));
    EXPECT_NE(queue_lookup, nullptr);
    EXPECT_NE(sink_lookup, nullptr);

    release_if_unparented(enabled_elements.queue_mp4);
    release_if_unparented(enabled_elements.parser_mp4);
    release_if_unparented(enabled_elements.muxer_mp4);
    release_if_unparented(enabled_elements.sink_mp4);
}

TEST_F(OutputBranchesFileNetworkTest, UdpBranchConfigurationAndLinkingWorksWithPreparedElements) {
    const auto functions = create_udp_branch_functions();

    auto disabled_config = make_base_config();
    disabled_config.communication.transport_type = sf::communication::transport_type::udp;
    global_data disabled_data(std::move(disabled_config));
    output_branch_facade facade_disabled_data(disabled_data);
    output_pipeline_elements disabled_elements{};
    EXPECT_FALSE(functions.is_enabled(facade_disabled_data));
    EXPECT_TRUE(functions.create_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.configure_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.link_branch(disabled_elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, disabled_elements));
    EXPECT_TRUE(functions.setup_signals(facade_disabled_data, disabled_elements));
    EXPECT_STREQ(functions.get_branch_name(), "udp");

    auto enabled_config = make_base_config();
    enabled_config.framerate = 0;
    enabled_config.communication.transport_type = sf::communication::transport_type::udp;
    enabled_config.communication.host = "127.0.0.1";
    enabled_config.communication.port = 5004;
    global_data enabled_data(std::move(enabled_config));
    output_branch_facade facade_enabled_data(enabled_data);
    enabled_data.assigned_rtp_port.store(6200);

    output_pipeline_elements enabled_elements{};
    enabled_elements.queue_rtp = gst_element_factory_make("queue", "q-rtp");
    enabled_elements.rtp_payloader = gst_element_factory_make("rtph265pay", "rtp-pay");
    enabled_elements.sink_rtp = gst_element_factory_make("udpsink", "s-rtp");
    if (enabled_elements.queue_rtp == nullptr || enabled_elements.rtp_payloader == nullptr ||
        enabled_elements.sink_rtp == nullptr) {
        release_if_unparented(enabled_elements.queue_rtp);
        release_if_unparented(enabled_elements.rtp_payloader);
        release_if_unparented(enabled_elements.sink_rtp);
        GTEST_SKIP() << "Required UDP branch plugins unavailable in this environment";
    }

    ASSERT_TRUE(functions.is_enabled(facade_enabled_data));
    ASSERT_TRUE(functions.configure_elements(facade_enabled_data, enabled_elements));

    guint max_size_buffers = 0;
    gint leaky = -1;
    gint pt = -1;
    gint config_interval = -1;
    gint port = -1;
    gchar *host = nullptr;
    g_object_get(G_OBJECT(enabled_elements.queue_rtp), "max-size-buffers", &max_size_buffers, "leaky", &leaky, nullptr);
    g_object_get(G_OBJECT(enabled_elements.rtp_payloader), "pt", &pt, "config-interval", &config_interval, nullptr);
    g_object_get(G_OBJECT(enabled_elements.sink_rtp), "host", &host, "port", &port, nullptr);
    EXPECT_EQ(max_size_buffers, 120U);
    EXPECT_EQ(leaky, static_cast<int>(queue_leaky_mode::no_leak));
    EXPECT_EQ(pt, 96);
    EXPECT_EQ(config_interval, 1);
    ASSERT_NE(host, nullptr);
    EXPECT_STREQ(host, "127.0.0.1");
    EXPECT_EQ(port, 6200);
    g_free(host);

    ASSERT_TRUE(functions.link_branch(enabled_elements));
    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "test-udp-tee"));
    ASSERT_NE(tee, nullptr);
    ASSERT_TRUE(functions.link_to_tee(tee.get(), enabled_elements));

    enabled_data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-udp-pipeline"));
    ASSERT_NE(enabled_data.output_pipeline, nullptr);
    functions.add_to_pipeline(facade_enabled_data, enabled_elements);

    sf::unique_gst_element_ptr queue_lookup(gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "q-rtp"));
    sf::unique_gst_element_ptr sink_lookup(gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "s-rtp"));
    EXPECT_NE(queue_lookup, nullptr);
    EXPECT_NE(sink_lookup, nullptr);

    release_if_unparented(enabled_elements.queue_rtp);
    release_if_unparented(enabled_elements.rtp_payloader);
    release_if_unparented(enabled_elements.sink_rtp);
}

TEST_F(OutputBranchesFileNetworkTest, WebRtcAndDisplayBranchesHandleDisabledAndNoopSignalPaths) {
    const auto webrtc_functions = create_webrtc_branch_functions();
    const auto display_functions = create_display_branch_functions();

    auto base_config = make_base_config();
    base_config.communication.transport_type = sf::communication::transport_type::webrtc;
    base_config.communication.host = "127.0.0.1";
    base_config.communication.port = 5006;
    base_config.webrtc.signaling_url = "ws://localhost:9999";
    base_config.debug.enable_display_branch = true;
    global_data data(std::move(base_config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    ASSERT_TRUE(webrtc_functions.is_enabled(facade_data));
    EXPECT_TRUE(webrtc_functions.configure_elements(facade_data, elements));
    EXPECT_TRUE(webrtc_functions.link_branch(elements));
    EXPECT_TRUE(webrtc_functions.link_to_tee(nullptr, elements));
    EXPECT_TRUE(webrtc_functions.setup_signals(facade_data, elements));
    EXPECT_STREQ(webrtc_functions.get_branch_name(), "webrtc");

    // Display branch no-op signal path: enabled but no overlays/borders requested.
    EXPECT_TRUE(display_functions.is_enabled(facade_data));
    EXPECT_TRUE(display_functions.configure_elements(facade_data, elements));
    EXPECT_TRUE(display_functions.link_branch(elements));
    EXPECT_TRUE(display_functions.link_to_tee(nullptr, elements));
    EXPECT_TRUE(display_functions.setup_signals(facade_data, elements));
    EXPECT_STREQ(display_functions.get_branch_name(), "display");
}
