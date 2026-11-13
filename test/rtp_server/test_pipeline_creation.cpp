// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <memory>

#include <gtest/gtest.h>

#include "../../libvcai/include/gst_raii.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../../rtp_server/src/pipeline/pipeline/creation.h"
#include "../../rtp_server/src/transport/transport_interface.h"
#include "../common/gst_test_fixture_rtp_server.h"

[[nodiscard]] bool validate_elements(const rtp_pipeline &pipeline);

namespace {
    struct pipeline_creation_test : GStreamerTestFixtureRtpServer {
        GMainLoop *test_loop = nullptr;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            test_loop = g_main_loop_new(nullptr, FALSE);
        }

        void TearDown() override {
            if (test_loop) {
                g_main_loop_unref(test_loop);
                test_loop = nullptr;
            }
            GStreamerTestFixtureRtpServer::TearDown();
        }

        [[nodiscard]] static transport_context make_transport(transport_type type) {
            transport_context ctx{};
            ctx.functions.get_transport_type = [type]() { return type; };
            return ctx;
        }

        [[nodiscard]] std::unique_ptr<rtp_pipeline> make_pipeline(transport_context ctx) const {
            return std::make_unique<rtp_pipeline>(5004, test_loop, rtp_output_config{}, std::nullopt,
                                                  sf::background_color::green, std::move(ctx), false, false, false,
                                                  nullptr);
        }
    };
} // namespace

TEST_F(pipeline_creation_test, create_elements_fails_when_transport_create_callback_missing) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    EXPECT_FALSE(create_elements(*pipeline));
}

TEST_F(pipeline_creation_test, create_elements_fails_when_transport_create_callback_returns_false) {
    auto ctx = make_transport(transport_type::rtp);
    ctx.functions.create_input_elements = [](transport_build_context *) { return false; };

    auto pipeline = make_pipeline(std::move(ctx));
    ASSERT_NE(pipeline, nullptr);

    EXPECT_FALSE(create_elements(*pipeline));
}

TEST_F(pipeline_creation_test, validate_elements_fails_for_uninitialized_pipeline) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    EXPECT_FALSE(validate_elements(*pipeline));
}

TEST_F(pipeline_creation_test, setup_webrtcbin_signals_requires_signaling_server_for_webrtc_transport) {
    auto pipeline = make_pipeline(make_transport(transport_type::webrtc));
    ASSERT_NE(pipeline, nullptr);

    pipeline->webrtcbin =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-webrtcbin-element"));
    ASSERT_NE(pipeline->webrtcbin.get(), nullptr);

    EXPECT_FALSE(setup_webrtcbin_signals(pipeline->transport_build_ctx, nullptr, "client-1"));
}

TEST_F(pipeline_creation_test, configure_elements_with_minimal_required_elements_succeeds_without_bridge_signals) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    pipeline->queue_encoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "test-q-enc"));
    pipeline->queue_decoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "test-q-dec"));
    pipeline->h265_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "test-h265-caps"));
    pipeline->decoded_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "test-decoded-caps"));
    pipeline->decoded_output_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "test-decoded-output-caps"));
    pipeline->sf_pri_reconstruct =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-pri"));
    pipeline->sf_eoi_luma_backscale =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-eoi"));
    pipeline->sf_tet_apply = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-tet"));

    ASSERT_NE(pipeline->queue_encoded.get(), nullptr);
    ASSERT_NE(pipeline->queue_decoded.get(), nullptr);
    ASSERT_NE(pipeline->h265_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->decoded_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->decoded_output_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->sf_pri_reconstruct.get(), nullptr);
    ASSERT_NE(pipeline->sf_eoi_luma_backscale.get(), nullptr);
    ASSERT_NE(pipeline->sf_tet_apply.get(), nullptr);

    EXPECT_TRUE(configure_elements(*pipeline));
    EXPECT_EQ(pipeline->runtime_context.signal_handler_infos.size(), 0U);
}

TEST_F(pipeline_creation_test, configure_elements_keeps_decoded_capsfilters_unfixed_for_metadata_propagation) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    pipeline->queue_encoded =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "test-q-enc-meta"));
    pipeline->queue_decoded =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "test-q-dec-meta"));
    pipeline->h265_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "test-h265-caps-meta"));
    pipeline->decoded_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "test-decoded-caps-meta"));
    pipeline->decoded_output_capsfilter = sf::unique_gst_element_no_delete_ptr(
            gst_element_factory_make("capsfilter", "test-decoded-output-caps-meta"));
    pipeline->sf_pri_reconstruct =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-pri-meta"));
    pipeline->sf_eoi_luma_backscale =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-eoi-meta"));
    pipeline->sf_tet_apply =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "test-tet-meta"));

    ASSERT_NE(pipeline->queue_encoded.get(), nullptr);
    ASSERT_NE(pipeline->queue_decoded.get(), nullptr);
    ASSERT_NE(pipeline->h265_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->decoded_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->decoded_output_capsfilter.get(), nullptr);

    EXPECT_TRUE(configure_elements(*pipeline));
    const auto bare_i420_caps = sf::unique_gst_caps_ptr(gst_caps_from_string("video/x-raw,format=I420"));
    ASSERT_NE(bare_i420_caps, nullptr);

    GstCaps *decoded_capsfilter_caps = nullptr;
    g_object_get(G_OBJECT(pipeline->decoded_capsfilter.get()), "caps", &decoded_capsfilter_caps, nullptr);
    ASSERT_NE(decoded_capsfilter_caps, nullptr);
    EXPECT_FALSE(gst_caps_is_equal(decoded_capsfilter_caps, bare_i420_caps.get()));
    gst_caps_unref(decoded_capsfilter_caps);

    GstCaps *decoded_output_caps = nullptr;
    g_object_get(G_OBJECT(pipeline->decoded_output_capsfilter.get()), "caps", &decoded_output_caps, nullptr);
    ASSERT_NE(decoded_output_caps, nullptr);
    EXPECT_FALSE(gst_caps_is_equal(decoded_output_caps, bare_i420_caps.get()));
    gst_caps_unref(decoded_output_caps);
}

TEST_F(pipeline_creation_test, add_elements_to_pipelines_adds_shared_pipeline_elements) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    pipeline->main_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("main-pipeline-test"));
    ASSERT_NE(pipeline->main_pipeline.get(), nullptr);

    pipeline->rtp_input_source = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("fakesrc", "rtp-src"));
    pipeline->rtp_input_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "rtp-caps"));
    pipeline->rtph265depay = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "depay"));
    pipeline->h265parse = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "parse"));
    pipeline->h265_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "h265-caps"));
    pipeline->queue_encoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "queue-encoded"));

    pipeline->sf_h265_sei_extract = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "sei"));
    pipeline->h265_decoder = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "decoder"));
    pipeline->decoded_converter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "decoded-convert"));
    pipeline->decoded_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "decoded-caps"));
    pipeline->sf_pri_reconstruct = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "pri"));
    pipeline->sf_eoi_luma_backscale = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "eoi"));
    pipeline->sf_tet_apply = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "tet"));
    pipeline->queue_decoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "queue-decoded"));
    pipeline->decoded_output_capsfilter =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "decoded-output-caps"));

    ASSERT_NE(pipeline->rtp_input_source.get(), nullptr);
    ASSERT_NE(pipeline->rtp_input_capsfilter.get(), nullptr);
    ASSERT_NE(pipeline->rtph265depay.get(), nullptr);
    ASSERT_NE(pipeline->h265parse.get(), nullptr);
    ASSERT_NE(pipeline->h265_capsfilter.get(), nullptr);
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

    EXPECT_TRUE(add_elements_to_pipelines(*pipeline));

    GstElement *queue_enc_from_bin = gst_bin_get_by_name(GST_BIN(pipeline->main_pipeline.get()), "queue-encoded");
    GstElement *queue_dec_from_bin = gst_bin_get_by_name(GST_BIN(pipeline->main_pipeline.get()), "queue-decoded");
    EXPECT_NE(queue_enc_from_bin, nullptr);
    EXPECT_NE(queue_dec_from_bin, nullptr);
    if (queue_enc_from_bin) {
        gst_object_unref(queue_enc_from_bin);
    }
    if (queue_dec_from_bin) {
        gst_object_unref(queue_dec_from_bin);
    }
}
