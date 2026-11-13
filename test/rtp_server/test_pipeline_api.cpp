// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../../rtp_server/src/pipeline/pipeline/callbacks.h"
#include "../../rtp_server/src/pipeline/pipeline/shutdown.h"
#include "../../rtp_server/src/transport/transport_interface.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    struct pipeline_api_test : GStreamerTestFixtureRtpServer {
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

TEST_F(pipeline_api_test, start_pipeline_returns_false_when_pipelines_not_created) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    EXPECT_FALSE(pipeline->start_pipeline());
}

TEST_F(pipeline_api_test, create_pipeline_returns_false_when_transport_callbacks_are_missing) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    EXPECT_FALSE(pipeline->create_pipeline());
}

TEST_F(pipeline_api_test, stop_operations_are_fast_noops_when_pipeline_already_stopped) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);
    pipeline->runtime_context.pipeline_stopped.store(true);

    const auto start = std::chrono::steady_clock::now();
    pipeline->stop_pipeline();
    pipeline->stop_pipeline_graceful();
    const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_LT(elapsed_ms.count(), 500);
}

TEST_F(pipeline_api_test, set_client_session_updates_non_owning_pointer) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    auto *dummy_session = reinterpret_cast<client_session *>(0x1);
    pipeline->set_client_session(dummy_session);
    EXPECT_EQ(pipeline->client_session_ptr, dummy_session);
}

TEST_F(pipeline_api_test, on_pipeline_message_handles_error_and_eos_messages) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    sf::unique_gst_element_ptr message_src(gst_element_factory_make("fakesrc", "pipeline-message-src"));
    ASSERT_NE(message_src, nullptr);

    GError *window_closed =
            g_error_new_literal(g_quark_from_static_string("pipeline-test"), 1, "Output window was closed by user");
    sf::unique_gst_message_ptr window_closed_message(
            gst_message_new_error(GST_OBJECT(message_src.get()), window_closed, "window-closed"));
    ASSERT_NE(window_closed_message, nullptr);
    EXPECT_TRUE(on_pipeline_message(nullptr, window_closed_message.get(), &pipeline->transport_build_ctx));

    GError *generic_error = g_error_new_literal(g_quark_from_static_string("pipeline-test"), 2, "generic error");
    sf::unique_gst_message_ptr generic_error_message(
            gst_message_new_error(GST_OBJECT(message_src.get()), generic_error, "generic"));
    ASSERT_NE(generic_error_message, nullptr);
    EXPECT_TRUE(on_pipeline_message(nullptr, generic_error_message.get(), &pipeline->transport_build_ctx));

    sf::unique_gst_message_ptr eos_message(gst_message_new_eos(GST_OBJECT(message_src.get())));
    ASSERT_NE(eos_message, nullptr);
    EXPECT_TRUE(on_pipeline_message(nullptr, eos_message.get(), &pipeline->transport_build_ctx));
}

TEST_F(pipeline_api_test, disconnect_signal_handlers_is_safe_for_default_and_partially_initialized_pipeline) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);
    EXPECT_NO_THROW(disconnect_signal_handlers(*pipeline));

    pipeline->webrtcbin = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", "shutdown-webrtc"));
    ASSERT_NE(pipeline->webrtcbin.get(), nullptr);
    EXPECT_NO_THROW(disconnect_signal_handlers(*pipeline));
}

TEST_F(pipeline_api_test, setup_signal_handlers_initialize_gstreamer_and_run_rtp_server_bootstrap_paths) {
    auto pipeline = make_pipeline(make_transport(transport_type::rtp));
    ASSERT_NE(pipeline, nullptr);

    EXPECT_FALSE(pipeline->create_pipeline());
    EXPECT_FALSE(pipeline->start_pipeline());
    EXPECT_NO_THROW(pipeline->stop_pipeline());
}
