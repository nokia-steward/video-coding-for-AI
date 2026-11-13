// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/bridge/app/encoder_app_adapter.h"
#include "../../encoder_cli/src/bridge/context/encoder_bridge_context.h"
#include "../../encoder_cli/src/bridge/orchestration/encoded_bridge_orchestrator.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.width = 16;
        config.height = 16;
        config.framerate = 30;
        config.format = sf::video_format::i420;
        return config;
    }

    [[nodiscard]] sf::unique_gst_buffer_ptr make_small_buffer() {
        auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 4, nullptr));
        if (!buffer) {
            return nullptr;
        }
        GstMapInfo map_info{};
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_WRITE)) {
            return nullptr;
        }
        map_info.data[0] = 0x00;
        map_info.data[1] = 0x00;
        map_info.data[2] = 0x01;
        map_info.data[3] = 0x26;
        gst_buffer_unmap(buffer.get(), &map_info);
        return buffer;
    }

    [[nodiscard]] sf::unique_gst_caps_ptr make_h265_caps(const char *alignment) {
        return sf::unique_gst_caps_ptr(gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING,
                                                           "byte-stream", "alignment", G_TYPE_STRING, alignment,
                                                           nullptr));
    }

    [[nodiscard]] bool setup_encoding_caps_contract(GstElement *appsrc, encoded_bridge_runtime &runtime,
                                                    GstCaps *caps) {
        if (appsrc == nullptr || caps == nullptr) {
            return false;
        }
        g_object_set(appsrc, "caps", caps, nullptr);
        if (!runtime.mark_startup_configured_once(true)) {
            return false;
        }
        return runtime.lock_caps_contract(true, caps);
    }
} // namespace

class EncodedBridgeOrchestratorTest : public GStreamerTestFixture {};

TEST_F(EncodedBridgeOrchestratorTest, FatalPushRequestsFailFastAndRollsBackPendingLumaState) {
    global_data data(make_base_config());
    data.stop_processing = false;
    data.push_pending_encoded_contract_state(1, {.enabled = true, .downscale_factor = 0.8});

    auto context =
            encoded_bridge_context{.encoding_src = nullptr, .sei_src = nullptr, .runtime = &data.egress_bridge_runtime};
    const encoder_app_adapter app_adapter(data);
    const encoded_bridge_push_options options{
            .update_output_counter = false,
            .update_sei_injection_counter = false,
            .update_encoder_submission_counter = false,
            .rollback_pending_luma_on_failure = true,
    };

    const auto result = encoded_bridge_push_owned_buffer_via_orchestrator(
            context, app_adapter, encoded_bridge_target::encoding_src, make_small_buffer(), options);
    EXPECT_EQ(result.status, encoded_bridge_step_status::fatal_stop);
    EXPECT_TRUE(data.stop_processing.load());
    EXPECT_FALSE(data.pop_pending_encoded_contract_state().has_value());
}

TEST_F(EncodedBridgeOrchestratorTest, SignalEosIsEmittedOncePerRuntimeInstance) {
    global_data data(make_base_config());
    data.stop_processing = false;

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("encoded-bridge-orchestrator-eos"));
    auto *appsrc = gst_element_factory_make("appsrc", "encoded-bridge-orchestrator-appsrc");
    auto *fakesink = gst_element_factory_make("fakesink", "encoded-bridge-orchestrator-fakesink");
    if (!pipeline || appsrc == nullptr || fakesink == nullptr) {
        if (appsrc) {
            gst_object_unref(appsrc);
        }
        if (fakesink) {
            gst_object_unref(fakesink);
        }
        GTEST_SKIP() << "Unable to create appsrc/fakesink pipeline";
    }

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, fakesink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, fakesink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, GST_SECOND);

    auto context =
            encoded_bridge_context{.encoding_src = appsrc, .sei_src = nullptr, .runtime = &data.egress_bridge_runtime};
    const encoder_app_adapter app_adapter(data);

    const auto first_result =
            encoded_bridge_signal_eos_via_orchestrator(context, app_adapter, encoded_bridge_target::encoding_src);
    EXPECT_EQ(first_result.status, encoded_bridge_step_status::ok);
    EXPECT_TRUE(data.egress_bridge_runtime.encoding_eos_sent.load());

    const auto second_result =
            encoded_bridge_signal_eos_via_orchestrator(context, app_adapter, encoded_bridge_target::encoding_src);
    EXPECT_EQ(second_result.status, encoded_bridge_step_status::ok);
    EXPECT_TRUE(data.egress_bridge_runtime.encoding_eos_sent.load());

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}

TEST_F(EncodedBridgeOrchestratorTest, RuntimeFlagsRemainIsolatedAcrossBridgeInstances) {
    encoded_bridge_runtime first_runtime{};
    encoded_bridge_runtime second_runtime{};

    auto first_context = encoded_bridge_context{.encoding_src = nullptr, .sei_src = nullptr, .runtime = &first_runtime};
    auto second_context =
            encoded_bridge_context{.encoding_src = nullptr, .sei_src = nullptr, .runtime = &second_runtime};

    first_runtime.encoding_eos_sent = true;
    EXPECT_TRUE(first_context.runtime->encoding_eos_sent.load());
    EXPECT_FALSE(second_context.runtime->encoding_eos_sent.load());

    second_runtime.sei_eos_sent = true;
    EXPECT_FALSE(first_context.runtime->sei_eos_sent.load());
    EXPECT_TRUE(second_context.runtime->sei_eos_sent.load());
}

TEST_F(EncodedBridgeOrchestratorTest, CapsMismatchTriggersFailFastStop) {
    global_data data(make_base_config());
    data.stop_processing = false;

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("encoded-bridge-orchestrator-caps-mismatch"));
    auto *appsrc = gst_element_factory_make("appsrc", "encoded-bridge-orchestrator-caps-appsrc");
    auto *fakesink = gst_element_factory_make("fakesink", "encoded-bridge-orchestrator-caps-fakesink");
    if (!pipeline || appsrc == nullptr || fakesink == nullptr) {
        if (appsrc) {
            gst_object_unref(appsrc);
        }
        if (fakesink) {
            gst_object_unref(fakesink);
        }
        GTEST_SKIP() << "Unable to create appsrc/fakesink pipeline";
    }

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, fakesink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, fakesink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, GST_SECOND);

    const auto startup_caps = make_h265_caps("au");
    const auto mismatched_caps = make_h265_caps("nal");
    ASSERT_TRUE(startup_caps);
    ASSERT_TRUE(mismatched_caps);
    ASSERT_TRUE(setup_encoding_caps_contract(appsrc, data.egress_bridge_runtime, startup_caps.get()));
    g_object_set(appsrc, "caps", mismatched_caps.get(), nullptr);

    auto context =
            encoded_bridge_context{.encoding_src = appsrc, .sei_src = nullptr, .runtime = &data.egress_bridge_runtime};
    const encoder_app_adapter app_adapter(data);
    const encoded_bridge_push_options options{
            .update_output_counter = false,
            .update_sei_injection_counter = false,
            .update_encoder_submission_counter = false,
            .rollback_pending_luma_on_failure = false,
    };

    const auto result = encoded_bridge_push_owned_buffer_via_orchestrator(
            context, app_adapter, encoded_bridge_target::encoding_src, make_small_buffer(), options);
    EXPECT_EQ(result.status, encoded_bridge_step_status::fatal_stop);
    EXPECT_TRUE(data.stop_processing.load());

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}
