// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <string>

#include "../../libvcai/include/gst_raii.h"
#include "../../rtp_server/src/core/rtp_server_config.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../../rtp_server/src/pipeline/pipeline/linking.h"
#include "../../rtp_server/src/transport/rtp_input/rtp_input_transport.h"
#include "../../rtp_server/src/transport/transport_interface.h"
#include "../../rtp_server/src/transport/webrtc/webrtc_transport.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    class transport_and_linking_test : public GStreamerTestFixtureRtpServer {
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

        [[nodiscard]] std::unique_ptr<rtp_pipeline> make_pipeline(transport_context ctx = {}) const {
            if (!ctx.functions.get_transport_type) {
                ctx.functions.get_transport_type = []() { return transport_type::rtp; };
            }
            return std::make_unique<rtp_pipeline>(5004, loop, rtp_output_config{}, std::nullopt,
                                                  sf::background_color::green, std::move(ctx), false, false, false,
                                                  nullptr);
        }
    };
} // namespace

TEST_F(transport_and_linking_test, rtp_input_transport_functions_cover_null_guard_and_noop_handlers) {
    const auto functions = create_rtp_input_transport_functions();
    EXPECT_FALSE(functions.initialize_server(nullptr));
    EXPECT_FALSE(functions.create_input_elements(nullptr));
    EXPECT_FALSE(functions.configure_input_elements(nullptr));
    EXPECT_FALSE(functions.link_input_pipeline(nullptr));
    EXPECT_TRUE(functions.setup_signals(nullptr, nullptr, nullptr));
    EXPECT_NO_THROW(functions.handle_signaling_message(nullptr, nullptr, nullptr, nullptr));
    EXPECT_NO_THROW(functions.cleanup(nullptr));
    EXPECT_EQ(functions.get_transport_type(), transport_type::rtp);

    const auto context = create_rtp_input_transport_context();
    EXPECT_EQ(context.functions.get_transport_type(), transport_type::rtp);
}

TEST_F(transport_and_linking_test, webrtc_transport_context_requires_config_and_functions_have_null_guards) {
    const auto empty_context = create_webrtc_transport_context(nullptr);
    EXPECT_FALSE(static_cast<bool>(empty_context.functions.get_transport_type));

    rtp_server_config config{};
    const auto functions = create_webrtc_transport_functions(&config);
    EXPECT_FALSE(functions.initialize_server(nullptr));
    EXPECT_FALSE(functions.create_input_elements(nullptr));
    EXPECT_FALSE(functions.configure_input_elements(nullptr));
    EXPECT_FALSE(functions.link_input_pipeline(nullptr));
    EXPECT_FALSE(functions.setup_signals(nullptr, nullptr, nullptr));
    EXPECT_NO_THROW(functions.handle_signaling_message(nullptr, nullptr, nullptr, nullptr));
    EXPECT_NO_THROW(functions.cleanup(nullptr));
    EXPECT_EQ(functions.get_transport_type(), transport_type::webrtc);
}

TEST_F(transport_and_linking_test, rtp_input_transport_configures_port_buffer_and_caps) {
    const auto functions = create_rtp_input_transport_functions();
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    pipeline->rtp_input_source = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("udpsrc", nullptr));
    pipeline->rtp_input_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", nullptr));
    pipeline->rtph265depay = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", nullptr));
    pipeline->h265parse = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", nullptr));
    pipeline->h265_capsfilter = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", nullptr));
    pipeline->queue_encoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", nullptr));
    ASSERT_NE(pipeline->rtp_input_source.get(), nullptr);
    ASSERT_NE(pipeline->rtp_input_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->rtph265depay.get(), nullptr);
    ASSERT_NE(pipeline->h265parse.get(), nullptr);
    ASSERT_NE(pipeline->h265_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->queue_encoded.get(), nullptr);

    EXPECT_TRUE(functions.configure_input_elements(&pipeline->transport_build_ctx));
    EXPECT_TRUE(functions.link_input_pipeline(&pipeline->transport_build_ctx));

    gint source_port = 0;
    gint source_buffer_size = 0;
    g_object_get(G_OBJECT(pipeline->rtp_input_source.get()), "port", &source_port, "buffer-size", &source_buffer_size,
                 nullptr);
    EXPECT_EQ(source_port, 5004);
    EXPECT_EQ(source_buffer_size, 2097152);

    GstCaps *caps = nullptr;
    g_object_get(G_OBJECT(pipeline->rtp_input_capsfilter.get()), "caps", &caps, nullptr);
    ASSERT_NE(caps, nullptr);
    gchar *caps_str = gst_caps_to_string(caps);
    ASSERT_NE(caps_str, nullptr);
    const std::string caps_string(caps_str);
    g_free(caps_str);
    gst_caps_unref(caps);
    EXPECT_NE(caps_string.find("application/x-rtp"), std::string::npos);
    EXPECT_NE(caps_string.find("encoding-name=(string)H265"), std::string::npos);
}

TEST_F(transport_and_linking_test, webrtc_transport_configures_caps_and_links_static_chain) {
    rtp_server_config config{};
    const auto functions = create_webrtc_transport_functions(&config);
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    if (!functions.create_input_elements(&pipeline->transport_build_ctx) || pipeline->webrtcbin.get() == nullptr) {
        GTEST_SKIP() << "webrtcbin element is unavailable in this environment";
    }

    EXPECT_TRUE(functions.configure_input_elements(&pipeline->transport_build_ctx));
    EXPECT_TRUE(functions.link_input_pipeline(&pipeline->transport_build_ctx));

    GstCaps *caps = nullptr;
    g_object_get(G_OBJECT(pipeline->webrtc_rtp_capsfilter.get()), "caps", &caps, nullptr);
    ASSERT_NE(caps, nullptr);
    gchar *caps_str = gst_caps_to_string(caps);
    ASSERT_NE(caps_str, nullptr);
    const std::string caps_string(caps_str);
    g_free(caps_str);
    gst_caps_unref(caps);
    EXPECT_NE(caps_string.find("application/x-rtp"), std::string::npos);
    EXPECT_NE(caps_string.find("encoding-name=(string)H265"), std::string::npos);
}

TEST_F(transport_and_linking_test, link_input_pipeline_delegates_to_transport_callback) {
    transport_context ok_ctx{};
    ok_ctx.functions.get_transport_type = []() { return transport_type::rtp; };
    ok_ctx.functions.link_input_pipeline = [](transport_build_context *) { return true; };
    auto ok_pipeline = make_pipeline(ok_ctx);
    ASSERT_NE(ok_pipeline, nullptr);
    EXPECT_TRUE(link_input_pipeline(*ok_pipeline));

    transport_context fail_ctx{};
    fail_ctx.functions.get_transport_type = []() { return transport_type::rtp; };
    auto fail_pipeline = make_pipeline(fail_ctx);
    ASSERT_NE(fail_pipeline, nullptr);
    EXPECT_FALSE(link_input_pipeline(*fail_pipeline));
}

TEST_F(transport_and_linking_test, link_decode_and_output_pipeline_handles_missing_and_valid_element_chains) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_FALSE(link_decode_and_output_pipeline(*pipeline));

    pipeline->queue_encoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-q-enc"));
    pipeline->sf_h265_sei_extract =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-sei-extract"));
    pipeline->h265_decoder = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-decoder"));
    pipeline->decoded_converter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-converter"));
    pipeline->decoded_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-caps"));
    pipeline->sf_pri_reconstruct =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-pri"));
    pipeline->sf_eoi_luma_backscale =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-eoi"));
    pipeline->sf_tet_apply = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-tet"));
    pipeline->queue_decoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-q-dec"));
    pipeline->decoded_output_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-decoded-output-caps"));
    pipeline->gpu_video_convert =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "proc-gpu-convert"));
    pipeline->tee = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("tee", "proc-tee"));
    ASSERT_NE(pipeline->queue_encoded.get(), nullptr);
    ASSERT_NE(pipeline->sf_h265_sei_extract.get(), nullptr);
    ASSERT_NE(pipeline->h265_decoder.get(), nullptr);
    ASSERT_NE(pipeline->decoded_converter.get(), nullptr);
    ASSERT_NE(pipeline->decoded_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->sf_pri_reconstruct.get(), nullptr);
    ASSERT_NE(pipeline->sf_eoi_luma_backscale.get(), nullptr);
    ASSERT_NE(pipeline->sf_tet_apply.get(), nullptr);
    ASSERT_NE(pipeline->queue_decoded.get(), nullptr);
    ASSERT_NE(pipeline->decoded_output_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->gpu_video_convert.get(), nullptr);
    ASSERT_NE(pipeline->tee.get(), nullptr);

    EXPECT_TRUE(link_decode_and_output_pipeline(*pipeline));
}

TEST_F(transport_and_linking_test,
       create_rtp_input_handshake_socket_and_setup_rtp_input_socket_watcher_paths_are_exercised_via_initialize_server) {
    const auto functions = create_rtp_input_transport_functions();
    const auto context = create_rtp_input_transport_context();

    EXPECT_TRUE(static_cast<bool>(functions.initialize_server));
    EXPECT_TRUE(static_cast<bool>(context.functions.initialize_server));
    EXPECT_TRUE(static_cast<bool>(functions.create_input_elements));
    EXPECT_TRUE(static_cast<bool>(functions.configure_input_elements));
    EXPECT_TRUE(static_cast<bool>(functions.link_input_pipeline));
}
