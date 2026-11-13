// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/output_branch_facade.h"
#include "../../encoder_cli/src/gst/output_pipeline/probes.h"
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

class OutputPipelineProbesTest : public GStreamerTestFixture {};

TEST_F(OutputPipelineProbesTest, ReturnsFalseWhenOutputPipelineIsNull) {
    auto config = make_base_config();
    global_data data(std::move(config));
    output_branch_facade facade(data);
    EXPECT_FALSE(setup_output_pipeline_probes(facade));
}

TEST_F(OutputPipelineProbesTest, ReturnsFalseWhenTeeElementIsMissing) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-output-pipeline"));
    ASSERT_NE(data.output_pipeline.get(), nullptr);

    output_branch_facade facade(data);
    EXPECT_FALSE(setup_output_pipeline_probes(facade));
}

TEST_F(OutputPipelineProbesTest, ReturnsTrueWhenCoreTeeProbeCanBeAttached) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-output-pipeline"));
    ASSERT_NE(data.output_pipeline.get(), nullptr);

    GstElement *tee = gst_element_factory_make("tee", "video-tee");
    ASSERT_NE(tee, nullptr);
    gst_bin_add(GST_BIN(data.output_pipeline.get()), tee);

    output_branch_facade facade(data);
    EXPECT_TRUE(setup_output_pipeline_probes(facade));
}

TEST_F(OutputPipelineProbesTest, SupportsWebRtcProbePathWhenEnabled) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-output-pipeline"));
    ASSERT_NE(data.output_pipeline.get(), nullptr);

    GstElement *tee = gst_element_factory_make("tee", "video-tee");
    GstElement *webrtcbin = gst_element_factory_make("identity", "webrtcbin");
    ASSERT_NE(tee, nullptr);
    ASSERT_NE(webrtcbin, nullptr);
    gst_bin_add_many(GST_BIN(data.output_pipeline.get()), tee, webrtcbin, nullptr);

    output_branch_facade facade(data);
    EXPECT_TRUE(setup_output_pipeline_probes(facade));
}
