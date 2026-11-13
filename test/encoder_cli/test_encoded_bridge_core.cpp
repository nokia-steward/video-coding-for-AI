// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file test_encoded_bridge_core.cpp
 * @brief Encoded appsrc bridge core tests (appsrc/appsink report §10: caps lock / post-lock mismatch → fatal_stop).
 */

#include <gst/app/gstappsrc.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/bridge/context/encoder_bridge_context.h"
#include "../../encoder_cli/src/bridge/core/encoded_bridge_core.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

namespace {
    struct appsrc_pipeline_handles {
        sf::unique_gst_element_ptr pipeline;
        GstElement *appsrc = nullptr;
    };

    [[nodiscard]] appsrc_pipeline_handles create_appsrc_pipeline(const char *pipeline_name, const char *appsrc_name) {
        appsrc_pipeline_handles handles{};
        handles.pipeline = sf::unique_gst_element_ptr(gst_pipeline_new(pipeline_name));
        if (!handles.pipeline) {
            return handles;
        }

        handles.appsrc = gst_element_factory_make("appsrc", appsrc_name);
        auto *fakesink = gst_element_factory_make("fakesink", "encoded-bridge-core-fakesink");
        if (!handles.appsrc || !fakesink) {
            if (handles.appsrc) {
                gst_object_unref(handles.appsrc);
                handles.appsrc = nullptr;
            }
            if (fakesink) {
                gst_object_unref(fakesink);
            }
            handles.pipeline.reset();
            return handles;
        }

        g_object_set(handles.appsrc, "is-live", FALSE, "do-timestamp", FALSE, "format", GST_FORMAT_TIME, nullptr);
        g_object_set(fakesink, "sync", FALSE, nullptr);
        gst_bin_add_many(GST_BIN(handles.pipeline.get()), handles.appsrc, fakesink, nullptr);
        if (!gst_element_link(handles.appsrc, fakesink)) {
            handles.pipeline.reset();
            handles.appsrc = nullptr;
            return handles;
        }

        return handles;
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

class EncodedBridgeCoreTest : public GStreamerTestFixture {};

TEST_F(EncodedBridgeCoreTest, PushOwnedBufferReturnsOkForPlayingEncodingAppsrc) {
    auto handles = create_appsrc_pipeline("encoded-bridge-core-pipeline-ok", "encoded-bridge-core-appsrc-ok");
    if (!handles.pipeline || !handles.appsrc) {
        GTEST_SKIP() << "Unable to create appsrc/fakesink pipeline";
    }
    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    const auto startup_caps = make_h265_caps("au");
    ASSERT_TRUE(startup_caps);
    encoded_bridge_runtime runtime{};
    ASSERT_TRUE(setup_encoding_caps_contract(handles.appsrc, runtime, startup_caps.get()));
    auto context = encoded_bridge_context{.encoding_src = handles.appsrc, .sei_src = nullptr, .runtime = &runtime};
    auto result = encoded_bridge_push_owned_buffer(context, encoded_bridge_target::encoding_src, make_small_buffer());
    EXPECT_EQ(result.status, encoded_bridge_step_status::ok);
    EXPECT_EQ(result.flow_return, GST_FLOW_OK);

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}

TEST_F(EncodedBridgeCoreTest, PushOwnedBufferReturnsRecoverableDropWhenPipelineIsFlushing) {
    auto handles =
            create_appsrc_pipeline("encoded-bridge-core-pipeline-flushing", "encoded-bridge-core-appsrc-flushing");
    if (!handles.pipeline || !handles.appsrc) {
        GTEST_SKIP() << "Unable to create appsrc/fakesink pipeline";
    }
    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);
    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    const auto startup_caps = make_h265_caps("au");
    ASSERT_TRUE(startup_caps);
    encoded_bridge_runtime runtime{};
    ASSERT_TRUE(setup_encoding_caps_contract(handles.appsrc, runtime, startup_caps.get()));
    auto context = encoded_bridge_context{.encoding_src = handles.appsrc, .sei_src = nullptr, .runtime = &runtime};
    auto result = encoded_bridge_push_owned_buffer(context, encoded_bridge_target::encoding_src, make_small_buffer());
    EXPECT_EQ(result.status, encoded_bridge_step_status::recoverable_drop);
    EXPECT_EQ(result.flow_return, GST_FLOW_FLUSHING);
}

TEST_F(EncodedBridgeCoreTest, PushOwnedBufferReturnsFatalStopForMissingAppsrc) {
    encoded_bridge_runtime runtime{};
    auto context = encoded_bridge_context{.encoding_src = nullptr, .sei_src = nullptr, .runtime = &runtime};
    auto result = encoded_bridge_push_owned_buffer(context, encoded_bridge_target::encoding_src, make_small_buffer());
    EXPECT_EQ(result.status, encoded_bridge_step_status::fatal_stop);
}

TEST_F(EncodedBridgeCoreTest, SignalEosIsIdempotentAndTracksPerTargetState) {
    auto handles = create_appsrc_pipeline("encoded-bridge-core-pipeline-eos", "encoded-bridge-core-appsrc-eos");
    if (!handles.pipeline || !handles.appsrc) {
        GTEST_SKIP() << "Unable to create appsrc/fakesink pipeline";
    }
    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    encoded_bridge_runtime runtime{};
    auto context = encoded_bridge_context{.encoding_src = handles.appsrc, .sei_src = nullptr, .runtime = &runtime};

    const auto first_result = encoded_bridge_signal_eos(context, encoded_bridge_target::encoding_src);
    EXPECT_EQ(first_result.status, encoded_bridge_step_status::ok);
    EXPECT_TRUE(runtime.encoding_eos_sent.load());

    const auto second_result = encoded_bridge_signal_eos(context, encoded_bridge_target::encoding_src);
    EXPECT_EQ(second_result.status, encoded_bridge_step_status::ok);
    EXPECT_TRUE(runtime.encoding_eos_sent.load());

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}

TEST_F(EncodedBridgeCoreTest, PushOwnedBufferReturnsFatalStopForPostLockCapsMismatch) {
    auto handles =
            create_appsrc_pipeline("encoded-bridge-core-pipeline-caps-mismatch", "encoded-bridge-core-appsrc-mismatch");
    if (!handles.pipeline || !handles.appsrc) {
        GTEST_SKIP() << "Unable to create appsrc/fakesink pipeline";
    }
    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    const auto locked_caps = make_h265_caps("au");
    const auto mismatched_caps = make_h265_caps("nal");
    ASSERT_TRUE(locked_caps);
    ASSERT_TRUE(mismatched_caps);

    encoded_bridge_runtime runtime{};
    ASSERT_TRUE(setup_encoding_caps_contract(handles.appsrc, runtime, locked_caps.get()));
    g_object_set(handles.appsrc, "caps", mismatched_caps.get(), nullptr);

    auto context = encoded_bridge_context{.encoding_src = handles.appsrc, .sei_src = nullptr, .runtime = &runtime};
    const auto result =
            encoded_bridge_push_owned_buffer(context, encoded_bridge_target::encoding_src, make_small_buffer());
    EXPECT_EQ(result.status, encoded_bridge_step_status::fatal_stop);
    EXPECT_EQ(result.flow_return, GST_FLOW_ERROR);

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}

TEST_F(EncodedBridgeCoreTest, PushOwnedBufferPreservesLockAcrossSuccessfulPushes) {
    auto handles = create_appsrc_pipeline("encoded-bridge-core-pipeline-lock", "encoded-bridge-core-appsrc-lock");
    if (!handles.pipeline || !handles.appsrc) {
        GTEST_SKIP() << "Unable to create appsrc/fakesink pipeline";
    }
    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    const auto startup_caps = make_h265_caps("au");
    ASSERT_TRUE(startup_caps);
    encoded_bridge_runtime runtime{};
    ASSERT_TRUE(setup_encoding_caps_contract(handles.appsrc, runtime, startup_caps.get()));

    auto context = encoded_bridge_context{.encoding_src = handles.appsrc, .sei_src = nullptr, .runtime = &runtime};
    const auto first_result =
            encoded_bridge_push_owned_buffer(context, encoded_bridge_target::encoding_src, make_small_buffer());
    const auto second_result =
            encoded_bridge_push_owned_buffer(context, encoded_bridge_target::encoding_src, make_small_buffer());

    EXPECT_EQ(first_result.status, encoded_bridge_step_status::ok);
    EXPECT_EQ(second_result.status, encoded_bridge_step_status::ok);
    EXPECT_TRUE(runtime.encoding_caps_locked.load());

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}
