// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../libvcai/include/gst_raii.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../../rtp_server/src/transport/transport_interface.h"
#include "../../rtp_server/src/transport/webrtc/webrtc_signaling.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    class webrtc_signaling_test : public GStreamerTestFixtureRtpServer {
    protected:
        GMainLoop *loop = nullptr;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            loop = g_main_loop_new(nullptr, FALSE);
        }

        void TearDown() override {
            if (loop != nullptr) {
                g_main_loop_unref(loop);
                loop = nullptr;
            }
            GStreamerTestFixtureRtpServer::TearDown();
        }

        [[nodiscard]] std::unique_ptr<rtp_pipeline> make_pipeline() const {
            transport_context ctx{};
            ctx.functions.get_transport_type = []() { return transport_type::webrtc; };
            return std::make_unique<rtp_pipeline>(5004, loop, rtp_output_config{}, std::nullopt,
                                                  sf::background_color::green, std::move(ctx), false, false, false,
                                                  nullptr);
        }
    };
} // namespace

TEST_F(webrtc_signaling_test, signaling_handler_returns_early_for_null_pipeline_or_webrtcbin) {
    const sf::communication::signaling_message offer{sf::communication::signaling_message_type::sdp_offer,
                                                     "not-an-sdp"};
    EXPECT_NO_THROW(handle_webrtc_signaling_message(nullptr, offer, "client-1", nullptr));

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_FALSE(pipeline->webrtcbin);
    EXPECT_NO_THROW(handle_webrtc_signaling_message(&pipeline->transport_build_ctx, offer, "client-1", nullptr));
}

TEST_F(webrtc_signaling_test, signaling_handler_handles_invalid_sdp_offer_without_crashing) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    pipeline->webrtcbin = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-webrtcbin"));
    ASSERT_NE(pipeline->webrtcbin.get(), nullptr);

    const sf::communication::signaling_message invalid_offer{sf::communication::signaling_message_type::sdp_offer,
                                                             "definitely-not-valid-sdp"};
    EXPECT_NO_THROW(
            handle_webrtc_signaling_message(&pipeline->transport_build_ctx, invalid_offer, "client-2", nullptr));
}
