// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/app/gstappsrc.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/bridge/core/ingress_bridge_core.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

namespace {
    struct appsink_pipeline_handles {
        sf::unique_gst_element_ptr pipeline;
        GstElement *appsrc = nullptr;
        GstElement *appsink = nullptr;
    };

    [[nodiscard]] appsink_pipeline_handles create_appsink_pipeline(const char *pipeline_name, const char *appsrc_name,
                                                                   const char *appsink_name) {
        appsink_pipeline_handles handles{};
        handles.pipeline = sf::unique_gst_element_ptr(gst_pipeline_new(pipeline_name));
        if (!handles.pipeline) {
            return handles;
        }

        handles.appsrc = gst_element_factory_make("appsrc", appsrc_name);
        handles.appsink = gst_element_factory_make("appsink", appsink_name);
        if (handles.appsrc == nullptr || handles.appsink == nullptr) {
            if (handles.appsrc != nullptr) {
                gst_object_unref(handles.appsrc);
                handles.appsrc = nullptr;
            }
            if (handles.appsink != nullptr) {
                gst_object_unref(handles.appsink);
                handles.appsink = nullptr;
            }
            handles.pipeline.reset();
            return handles;
        }

        g_object_set(handles.appsrc, "is-live", FALSE, "do-timestamp", FALSE, nullptr);
        g_object_set(handles.appsink, "emit-signals", TRUE, "sync", FALSE, nullptr);
        gst_bin_add_many(GST_BIN(handles.pipeline.get()), handles.appsrc, handles.appsink, nullptr);
        if (!gst_element_link(handles.appsrc, handles.appsink)) {
            handles.pipeline.reset();
            handles.appsrc = nullptr;
            handles.appsink = nullptr;
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
        map_info.data[3] = 0x65;
        gst_buffer_unmap(buffer.get(), &map_info);
        return buffer;
    }
} // namespace

class IngressBridgeCoreTest : public GStreamerTestFixture {};

TEST_F(IngressBridgeCoreTest, PullSampleFromSignalReturnsFatalStopForNullSink) {
    const auto result = ingress_bridge_pull_sample_from_signal(nullptr);
    EXPECT_EQ(result.status, ingress_bridge_step_status::fatal_stop);
    EXPECT_EQ(result.sample, nullptr);
}

TEST_F(IngressBridgeCoreTest, PullSampleFromSignalReturnsRecoverableDropWhenNoSampleAvailable) {
    const auto handles =
            create_appsink_pipeline("ingress-core-signal-empty", "ingress-core-empty-src", "ingress-core-empty-sink");
    if (!handles.pipeline || handles.appsink == nullptr) {
        GTEST_SKIP() << "Unable to create appsrc/appsink pipeline";
    }

    if (handles.appsrc == nullptr) {
        GTEST_SKIP() << "Unable to create appsrc/appsink pipeline";
    }
    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    // Drive the source to EOS so the sink has no sample: on a live appsink,
    // pull-sample would otherwise block forever waiting for data.
    ASSERT_EQ(gst_app_src_end_of_stream(GST_APP_SRC(handles.appsrc)), GST_FLOW_OK);

    const auto result = ingress_bridge_pull_sample_from_signal(handles.appsink);
    EXPECT_EQ(result.status, ingress_bridge_step_status::recoverable_drop);
    EXPECT_EQ(result.sample, nullptr);

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}

TEST_F(IngressBridgeCoreTest, PullSampleFromSignalReturnsSampleWhenAvailable) {
    const auto handles =
            create_appsink_pipeline("ingress-core-signal-ok", "ingress-core-ok-src", "ingress-core-ok-sink");
    if (!handles.pipeline || handles.appsrc == nullptr || handles.appsink == nullptr) {
        GTEST_SKIP() << "Unable to create appsrc/appsink pipeline";
    }

    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);
    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(handles.appsrc), make_small_buffer().release()), GST_FLOW_OK);

    const auto result = ingress_bridge_pull_sample_from_signal(handles.appsink);
    EXPECT_EQ(result.status, ingress_bridge_step_status::ok);
    EXPECT_NE(result.sample, nullptr);

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}

TEST_F(IngressBridgeCoreTest, TryPullSampleMapsTimeoutAndEosStates) {
    const auto handles =
            create_appsink_pipeline("ingress-core-try-pull", "ingress-core-try-src", "ingress-core-try-sink");
    if (!handles.pipeline || handles.appsrc == nullptr || handles.appsink == nullptr) {
        GTEST_SKIP() << "Unable to create appsrc/appsink pipeline";
    }

    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    const auto timeout_result = ingress_bridge_try_pull_sample(handles.appsink, 5 * GST_MSECOND);
    EXPECT_EQ(timeout_result.status, ingress_bridge_step_status::recoverable_drop);

    ASSERT_EQ(gst_app_src_end_of_stream(GST_APP_SRC(handles.appsrc)), GST_FLOW_OK);
    const auto eos_result = ingress_bridge_try_pull_sample(handles.appsink, 100 * GST_MSECOND);
    EXPECT_EQ(eos_result.status, ingress_bridge_step_status::drain_complete);

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}
