// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../rtp_server/src/core/rtp_server_config.h"
#include "../../rtp_server/src/transport/transport_factory.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    // Test fixture for transport_factory tests
    struct transport_factory_test : GStreamerTestFixtureRtpServer {
    protected:
        rtp_server_config config;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            config = rtp_server_config{};
        }
    };

    // Test case for creating RTP input transport context
    TEST_F(transport_factory_test, create_rtp_input_transport_context) {
        const auto context = create_transport_context(transport_type::rtp, &config);

        EXPECT_NE(context.functions.initialize_server, nullptr);
        EXPECT_NE(context.functions.create_input_elements, nullptr);
        EXPECT_NE(context.functions.setup_signals, nullptr);
        EXPECT_NE(context.functions.cleanup, nullptr);
    }

    // Test case for creating WebRTC transport context
    TEST_F(transport_factory_test, create_webrtc_transport_context) {
        config.communication_transport_type = transport_type::webrtc;
        config.webrtc_signaling_mode_enum = webrtc_signaling_mode::embedded;
        config.webrtc_signaling_port = 8080;

        const auto context = create_transport_context(transport_type::webrtc, &config);

        EXPECT_NE(context.functions.initialize_server, nullptr);
        EXPECT_NE(context.functions.create_input_elements, nullptr);
        EXPECT_NE(context.functions.setup_signals, nullptr);
        EXPECT_NE(context.functions.cleanup, nullptr);
    }
} // namespace
