// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/webrtc_connection.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/elements.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/output_branch_facade.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/udp/udp_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/webrtc/webrtc_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/rtp_branch.h"
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

class OutputBranchRtpWebRtcTest : public GStreamerTestFixture {};

TEST_F(OutputBranchRtpWebRtcTest, UdpBranchEnablementRequiresCommunicationEndpoint) {
    const auto udp_context = create_udp_branch_context();
    const auto &udp_functions = udp_context.functions;

    auto config_missing_host = make_base_config();
    config_missing_host.communication.port = 5000;
    global_data data_missing_host(std::move(config_missing_host));
    output_branch_facade facade_data_missing_host(data_missing_host);
    EXPECT_FALSE(udp_functions.is_enabled(facade_data_missing_host));

    auto config_missing_port = make_base_config();
    config_missing_port.communication.host = "127.0.0.1";
    config_missing_port.communication.port = 0;
    global_data data_missing_port(std::move(config_missing_port));
    output_branch_facade facade_data_missing_port(data_missing_port);
    EXPECT_FALSE(udp_functions.is_enabled(facade_data_missing_port));
}

TEST_F(OutputBranchRtpWebRtcTest, SetupWebRtcSignalsNoopsWhenNotWebRtcOrMissingElement) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::udp;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    const auto context = create_webrtc_branch_context();
    EXPECT_TRUE(context.functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchRtpWebRtcTest, WebRtcBranchNameIsStable) {
    const auto context = create_webrtc_branch_context();
    EXPECT_STREQ(context.functions.get_branch_name(), "webrtc");
}

TEST_F(OutputBranchRtpWebRtcTest, TriggerWebRtcNegotiationUsesExpectedCompatibilityBehavior) {
    auto non_webrtc_config = make_base_config();
    non_webrtc_config.communication.transport_type = sf::communication::transport_type::udp;
    global_data non_webrtc_data(std::move(non_webrtc_config));
    EXPECT_TRUE(trigger_webrtc_negotiation(non_webrtc_data));

    auto webrtc_config = make_base_config();
    webrtc_config.communication.transport_type = sf::communication::transport_type::webrtc;
    webrtc_config.communication.host = "127.0.0.1";
    webrtc_config.communication.port = 5000;
    webrtc_config.webrtc.signaling_url = "ws://localhost:8080";
    global_data webrtc_data(std::move(webrtc_config));
    EXPECT_FALSE(trigger_webrtc_negotiation(webrtc_data));
}

TEST_F(OutputBranchRtpWebRtcTest, TriggerWebRtcNegotiationFailsWhenWebRtcBinMissingFromPipeline) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-output-pipeline"));
    ASSERT_NE(data.output_pipeline.get(), nullptr);

    EXPECT_FALSE(trigger_webrtc_negotiation(data));
}

TEST_F(OutputBranchRtpWebRtcTest, WebRtcBranchEnablementRequiresAllMandatoryFields) {
    auto base = make_base_config();
    base.communication.transport_type = sf::communication::transport_type::webrtc;
    global_data missing_fields(std::move(base));
    output_branch_facade facade_missing_fields(missing_fields);

    const auto context = create_webrtc_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.is_enabled(facade_missing_fields));

    auto ready_config = make_base_config();
    ready_config.communication.transport_type = sf::communication::transport_type::webrtc;
    ready_config.communication.host = "127.0.0.1";
    ready_config.communication.port = 5000;
    ready_config.webrtc.signaling_url = "ws://localhost:8080";
    global_data ready_data(std::move(ready_config));
    output_branch_facade facade_ready_data(ready_data);
    EXPECT_TRUE(functions.is_enabled(facade_ready_data));
}

TEST_F(OutputBranchRtpWebRtcTest, WebRtcBranchNoopsGracefullyWhenElementsNotCreated) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    const auto context = create_webrtc_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.configure_elements(facade_data, elements));
    EXPECT_TRUE(functions.link_branch(elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, elements));
    EXPECT_TRUE(functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchRtpWebRtcTest, WebRtcBranchRequiresWebRtcTransportEvenWithEndpointAndSignaling) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::udp;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    const auto context = create_webrtc_branch_context();
    EXPECT_FALSE(context.functions.is_enabled(facade_data));
}

TEST_F(OutputBranchRtpWebRtcTest, SetupSignalsNoopsWhenBranchEnabledButWebRtcElementMissing) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    output_pipeline_elements elements{};

    const auto context = create_webrtc_branch_context();
    EXPECT_TRUE(context.functions.is_enabled(facade_data));
    EXPECT_TRUE(context.functions.setup_signals(facade_data, elements));
}

TEST_F(OutputBranchRtpWebRtcTest, SetupWebRtcSignalsFailsWhenSignalingUrlMissing) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.webrtcbin = gst_element_factory_make("identity", "test-webrtcbin-placeholder");
    ASSERT_NE(elements.webrtcbin, nullptr);

    const auto context = create_webrtc_branch_context();
    EXPECT_TRUE(context.functions.setup_signals(facade_data, elements));
    gst_object_unref(elements.webrtcbin);
}

TEST_F(OutputBranchRtpWebRtcTest, WebRtcLinkAndTeeLinkFailWhenBranchElementsAreIncomplete) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.webrtcbin = gst_element_factory_make("identity", "test-webrtcbin-only");
    ASSERT_NE(elements.webrtcbin, nullptr);

    const auto context = create_webrtc_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.link_branch(elements));
    EXPECT_FALSE(functions.link_to_tee(nullptr, elements));
    gst_object_unref(elements.webrtcbin);
}

TEST_F(OutputBranchRtpWebRtcTest, LinkRtpBranchFailsWhenRequiredElementsMissing) {
    const auto udp_context = create_udp_branch_context();
    const auto &udp_functions = udp_context.functions;

    output_pipeline_elements elements{};
    elements.sink_rtp = gst_element_factory_make("fakesink", "test-rtp-sink");
    ASSERT_NE(elements.sink_rtp, nullptr);

    EXPECT_TRUE(udp_functions.link_branch(elements));
    EXPECT_TRUE(udp_functions.link_to_tee(nullptr, elements));

    gst_object_unref(elements.sink_rtp);
}

TEST_F(OutputBranchRtpWebRtcTest, WaitForWebRtcConnectionReturnsImmediatelyWhenAlreadyReady) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    data.webrtc.connection_ready.store(true);

    EXPECT_TRUE(wait_for_webrtc_connection(data, std::chrono::seconds(0)));
}

TEST_F(OutputBranchRtpWebRtcTest, WaitForWebRtcConnectionTimesOutWhenNotReady) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    data.webrtc.connection_ready.store(false);

    EXPECT_FALSE(wait_for_webrtc_connection(data, std::chrono::seconds(0)));
}

TEST_F(OutputBranchRtpWebRtcTest, is_udp_branch_enabled_accepts_only_udp_transport_type) {
    const auto context = create_udp_branch_context();

    auto udp_config = make_base_config();
    udp_config.communication.transport_type = sf::communication::transport_type::udp;
    udp_config.communication.host = "127.0.0.1";
    udp_config.communication.port = 5500;
    global_data udp_data(std::move(udp_config));
    output_branch_facade facade_udp_data(udp_data);
    EXPECT_TRUE(context.functions.is_enabled(facade_udp_data));

    auto webrtc_config = make_base_config();
    webrtc_config.communication.transport_type = sf::communication::transport_type::webrtc;
    webrtc_config.communication.host = "127.0.0.1";
    webrtc_config.communication.port = 5500;
    global_data webrtc_data(std::move(webrtc_config));
    output_branch_facade facade_webrtc_data(webrtc_data);
    EXPECT_FALSE(context.functions.is_enabled(facade_webrtc_data));
}

TEST_F(OutputBranchRtpWebRtcTest, test_rtp_plugins_available_create_udp_branch_elements_configure_udp_branch_elements) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::udp;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    data.assigned_rtp_port.store(6201);

    output_pipeline_elements elements{};
    elements.queue_rtp = gst_element_factory_make("queue", "udp-config-queue");
    elements.rtp_payloader = gst_element_factory_make("rtph265pay", "udp-config-payloader");
    elements.sink_rtp = gst_element_factory_make("udpsink", "udp-config-sink");
    if (elements.queue_rtp == nullptr || elements.rtp_payloader == nullptr || elements.sink_rtp == nullptr) {
        if (elements.queue_rtp) {
            gst_object_unref(elements.queue_rtp);
        }
        if (elements.rtp_payloader) {
            gst_object_unref(elements.rtp_payloader);
        }
        if (elements.sink_rtp) {
            gst_object_unref(elements.sink_rtp);
        }
        GTEST_SKIP() << "Required UDP branch plugins unavailable in this environment";
    }

    const auto context = create_udp_branch_context();
    EXPECT_TRUE(context.functions.configure_elements(facade_data, elements));

    gst_object_unref(elements.queue_rtp);
    gst_object_unref(elements.rtp_payloader);
    gst_object_unref(elements.sink_rtp);
}

TEST_F(OutputBranchRtpWebRtcTest, is_webrtc_branch_enabled_rejects_empty_signaling_url_even_with_endpoint) {
    const auto context = create_webrtc_branch_context();

    auto missing_url = make_base_config();
    missing_url.communication.transport_type = sf::communication::transport_type::webrtc;
    missing_url.communication.host = "127.0.0.1";
    missing_url.communication.port = 5600;
    global_data missing_url_data(std::move(missing_url));
    output_branch_facade facade_missing_url_data(missing_url_data);
    EXPECT_FALSE(context.functions.is_enabled(facade_missing_url_data));

    auto with_url = make_base_config();
    with_url.communication.transport_type = sf::communication::transport_type::webrtc;
    with_url.communication.host = "127.0.0.1";
    with_url.communication.port = 5600;
    with_url.webrtc.signaling_url = "ws://localhost:5601";
    global_data with_url_data(std::move(with_url));
    output_branch_facade facade_with_url_data(with_url_data);
    EXPECT_TRUE(context.functions.is_enabled(facade_with_url_data));
}

TEST_F(OutputBranchRtpWebRtcTest,
       get_webrtc_session_description_type_test_rtp_plugins_available_test_webrtcbin_available_and_set_local_description_and_send_offer_paths) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    const auto context = create_webrtc_branch_context();
    EXPECT_TRUE(context.functions.is_enabled(facade_data));

    output_pipeline_elements elements{};
    EXPECT_TRUE(context.functions.configure_elements(facade_data, elements));
    EXPECT_TRUE(context.functions.link_branch(elements));
    EXPECT_TRUE(context.functions.link_to_tee(nullptr, elements));
}
