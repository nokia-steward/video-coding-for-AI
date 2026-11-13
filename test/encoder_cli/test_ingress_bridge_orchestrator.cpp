// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/app/gstappsrc.h>
#include <gtest/gtest.h>

#include <optional>

#include "../../encoder_cli/src/bridge/app/ingress_app_adapter.h"
#include "../../encoder_cli/src/bridge/context/ingress_bridge_context.h"
#include "../../encoder_cli/src/bridge/orchestration/ingress_bridge_orchestrator.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

GST_DEBUG_CATEGORY_STATIC(test_ingress_bridge_orchestrator);

namespace {
    struct appsink_pipeline_handles {
        sf::unique_gst_element_ptr pipeline;
        GstElement *appsrc = nullptr;
        GstElement *appsink = nullptr;
    };

    void init_test_category() {
        static bool is_initialized = false;
        if (!is_initialized) {
            GST_DEBUG_CATEGORY_INIT(test_ingress_bridge_orchestrator, "test-ingress-bridge-orchestrator", 0,
                                    "Tests for ingress bridge orchestrator");
            is_initialized = true;
        }
    }

    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::camera;
        config.width = 16;
        config.height = 16;
        config.framerate = 30;
        config.format = sf::video_format::i420;
        return config;
    }

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

class IngressBridgeOrchestratorTest : public GStreamerTestFixture {};

TEST_F(IngressBridgeOrchestratorTest, RawCallbackDropCounterIsPerInstance) {
    init_test_category();
    auto handles1 =
            create_appsink_pipeline("ingress-orch-drop-1", "ingress-orch-drop-src-1", "ingress-orch-drop-sink-1");
    auto handles2 =
            create_appsink_pipeline("ingress-orch-drop-2", "ingress-orch-drop-src-2", "ingress-orch-drop-sink-2");
    if (!handles1.pipeline || !handles2.pipeline || handles1.appsrc == nullptr || handles1.appsink == nullptr ||
        handles2.appsrc == nullptr || handles2.appsink == nullptr) {
        GTEST_SKIP() << "Unable to create appsrc/appsink pipelines";
    }

    ASSERT_NE(gst_element_set_state(handles1.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    ASSERT_NE(gst_element_set_state(handles2.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles1.pipeline.get(), nullptr, nullptr, GST_SECOND);
    gst_element_get_state(handles2.pipeline.get(), nullptr, nullptr, GST_SECOND);

    global_data data1(make_base_config());
    global_data data2(make_base_config());
    data1.ready_to_process_frames = false;
    data2.ready_to_process_frames = false;

    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(handles1.appsrc), make_small_buffer().release()), GST_FLOW_OK);
    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(handles2.appsrc), make_small_buffer().release()), GST_FLOW_OK);

    auto context1 = ingress_bridge_context{.runtime = &data1.ingress_runtime};
    auto context2 = ingress_bridge_context{.runtime = &data2.ingress_runtime};
    const ingress_app_adapter adapter1(data1);
    const ingress_app_adapter adapter2(data2);

    EXPECT_EQ(ingress_handle_raw_new_sample_via_orchestrator(context1, adapter1, handles1.appsink,
                                                             test_ingress_bridge_orchestrator),
              GST_FLOW_OK);
    EXPECT_EQ(ingress_handle_raw_new_sample_via_orchestrator(context2, adapter2, handles2.appsink,
                                                             test_ingress_bridge_orchestrator),
              GST_FLOW_OK);
    EXPECT_EQ(data1.ingress_runtime.dropped_raw_sample_count.load(), 1U);
    EXPECT_EQ(data2.ingress_runtime.dropped_raw_sample_count.load(), 1U);

    gst_element_set_state(handles1.pipeline.get(), GST_STATE_NULL);
    gst_element_set_state(handles2.pipeline.get(), GST_STATE_NULL);
}

TEST_F(IngressBridgeOrchestratorTest, RawAndEncodedEosAreClosedOncePerInstance) {
    init_test_category();
    global_data data(make_base_config());
    auto context = ingress_bridge_context{.runtime = &data.ingress_runtime};
    const ingress_app_adapter adapter(data);

    ingress_handle_raw_eos_via_orchestrator(context, adapter, test_ingress_bridge_orchestrator);
    ingress_handle_raw_eos_via_orchestrator(context, adapter, test_ingress_bridge_orchestrator);
    EXPECT_TRUE(data.ingress_runtime.raw_queue_closed.load());
    EXPECT_FALSE(data.raw_frame_queue.pop().has_value());

    ingress_handle_encoded_eos_via_orchestrator(context, adapter, test_ingress_bridge_orchestrator);
    ingress_handle_encoded_eos_via_orchestrator(context, adapter, test_ingress_bridge_orchestrator);
    EXPECT_TRUE(data.ingress_runtime.encoded_queue_closed.load());
    EXPECT_FALSE(data.encoded_frame_queue.pop().has_value());
}

TEST_F(IngressBridgeOrchestratorTest, EncodedCallbackConsumesPendingLumaStateAndQueuesItem) {
    init_test_category();
    auto handles =
            create_appsink_pipeline("ingress-orch-encoded", "ingress-orch-encoded-src", "ingress-orch-encoded-sink");
    if (!handles.pipeline || handles.appsrc == nullptr || handles.appsink == nullptr) {
        GTEST_SKIP() << "Unable to create appsrc/appsink pipeline";
    }

    ASSERT_NE(gst_element_set_state(handles.pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(handles.pipeline.get(), nullptr, nullptr, GST_SECOND);

    global_data data(make_base_config());
    data.push_pending_encoded_contract_state(
            1, {.enabled = true, .downscale_factor = 0.7, .upscale_multiplier = std::nullopt});
    auto context = ingress_bridge_context{.runtime = &data.ingress_runtime};
    const ingress_app_adapter adapter(data);

    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(handles.appsrc), make_small_buffer().release()), GST_FLOW_OK);
    EXPECT_EQ(ingress_handle_encoded_new_sample_via_orchestrator(context, adapter, handles.appsink,
                                                                 test_ingress_bridge_orchestrator),
              GST_FLOW_OK);

    const auto item_opt = data.encoded_frame_queue.pop();
    ASSERT_TRUE(item_opt.has_value());
    EXPECT_EQ(item_opt->contract_sequence_id, 1U);
    EXPECT_TRUE(item_opt->luma_adaptation.enabled);
    EXPECT_DOUBLE_EQ(item_opt->luma_adaptation.downscale_factor, 0.7);

    gst_element_set_state(handles.pipeline.get(), GST_STATE_NULL);
}

TEST_F(IngressBridgeOrchestratorTest, FatalPollingPullRequestsFailFastStop) {
    init_test_category();
    global_data data(make_base_config());
    data.stop_processing = false;
    auto context = ingress_bridge_context{.runtime = &data.ingress_runtime};
    const ingress_app_adapter adapter(data);

    const auto pull_result = ingress_try_pull_sample_via_orchestrator(context, adapter, nullptr, 5 * GST_MSECOND);
    EXPECT_EQ(pull_result.status, ingress_bridge_step_status::fatal_stop);
    EXPECT_TRUE(data.stop_processing.load());
}
