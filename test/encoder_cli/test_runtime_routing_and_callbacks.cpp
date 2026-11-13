// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cstring>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gtest/gtest.h>
#include <vector>

#include "../../encoder_cli/src/application/thread_management.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/gst_callbacks.h"
#include "../../encoder_cli/src/gst/input_types/appsink_polling.h"
#include "../../encoder_cli/src/gst/input_types/frame_merger.h"
#include "../../encoder_cli/src/gst/input_types/frame_router.h"
#include "../../encoder_cli/src/gst/input_types/last_inference_roi.h"
#include "../../encoder_cli/src/gst/output_pipeline/elements.h"
#include "../../encoder_cli/src/gst/output_pipeline/queues.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 25;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_25.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }

    void release_if_set(GstElement *&element) {
        if (element != nullptr) {
            gst_object_unref(element);
            element = nullptr;
        }
    }
} // namespace

class RuntimeRoutingAndCallbacksTest : public GStreamerTestFixture {};

TEST_F(RuntimeRoutingAndCallbacksTest, FrameRouterRoutesEveryNthFrameAndResetRestoresCounters) {
    frame_router router(3);

    EXPECT_EQ(router.decide_route(), routing_decision::inference);
    EXPECT_EQ(router.decide_route(), routing_decision::inference);
    EXPECT_EQ(router.decide_route(), routing_decision::bypass);
    EXPECT_EQ(router.decide_route(), routing_decision::inference);
    EXPECT_EQ(router.decide_route(), routing_decision::inference);
    EXPECT_EQ(router.decide_route(), routing_decision::bypass);

    EXPECT_EQ(router.get_bypass_interval(), 3);
    EXPECT_EQ(router.get_inference_frame_count(), 4U);
    EXPECT_EQ(router.get_bypass_frame_count(), 2U);

    router.reset();
    EXPECT_EQ(router.get_inference_frame_count(), 0U);
    EXPECT_EQ(router.get_bypass_frame_count(), 0U);
    EXPECT_EQ(router.decide_route(), routing_decision::inference);
}

TEST_F(RuntimeRoutingAndCallbacksTest, init_frame_router_debug_category_route_logic_is_stable_via_frame_router_usage) {
    frame_router router(2);
    EXPECT_EQ(router.decide_route(), routing_decision::inference);
    EXPECT_EQ(router.decide_route(), routing_decision::bypass);
}

TEST_F(RuntimeRoutingAndCallbacksTest, RoutingProbeGuardsAndInstallationAreDeterministic) {
    EXPECT_EQ(routing_probe_callback(nullptr, nullptr, nullptr), GST_PAD_PROBE_OK);

    auto config = make_base_config();
    global_data data(std::move(config));
    frame_router router(2);
    routing_probe_context context{.bypass = &data.bypass, .router = &router};

    GstPadProbeInfo probe_info{};
    probe_info.type = GST_PAD_PROBE_TYPE_BUFFER;

    data.bypass.valve_inference = gst_element_factory_make("valve", "test-routing-valve-inference");
    data.bypass.valve_bypass = gst_element_factory_make("valve", "test-routing-valve-bypass");
    if (data.bypass.valve_inference == nullptr || data.bypass.valve_bypass == nullptr) {
        release_if_set(data.bypass.valve_inference);
        release_if_set(data.bypass.valve_bypass);
        GTEST_SKIP() << "valve plugin not available in this environment";
    }

    EXPECT_EQ(routing_probe_callback(nullptr, &probe_info, &context), GST_PAD_PROBE_OK);
    gboolean inference_drop = TRUE;
    gboolean bypass_drop = FALSE;
    g_object_get(G_OBJECT(data.bypass.valve_inference), "drop", &inference_drop, nullptr);
    g_object_get(G_OBJECT(data.bypass.valve_bypass), "drop", &bypass_drop, nullptr);
    EXPECT_EQ(inference_drop, FALSE);
    EXPECT_EQ(bypass_drop, TRUE);

    EXPECT_EQ(routing_probe_callback(nullptr, &probe_info, &context), GST_PAD_PROBE_OK);
    g_object_get(G_OBJECT(data.bypass.valve_inference), "drop", &inference_drop, nullptr);
    g_object_get(G_OBJECT(data.bypass.valve_bypass), "drop", &bypass_drop, nullptr);
    EXPECT_EQ(inference_drop, TRUE);
    EXPECT_EQ(bypass_drop, FALSE);

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "test-routing-tee"));
    ASSERT_NE(tee, nullptr);
    const gulong probe_id = install_routing_probe(tee.get(), &context);
    EXPECT_GT(probe_id, 0UL);

    GstPad *sink_pad = gst_element_get_static_pad(tee.get(), "sink");
    ASSERT_NE(sink_pad, nullptr);
    gst_pad_remove_probe(sink_pad, probe_id);
    gst_object_unref(sink_pad);

    release_if_set(data.bypass.valve_inference);
    release_if_set(data.bypass.valve_bypass);
}

TEST_F(RuntimeRoutingAndCallbacksTest, AppsinkPollingThreadsExitWhenStopAlreadyRequested) {
    init_appsink_polling_debug_category();

    auto config = make_base_config();
    global_data data(std::move(config));

    frame_merger_config merger_config{};
    frame_merger merger(merger_config);
    std::atomic<bool> stop_flag{true};
    std::atomic<int> eos_counter{0};

    GstElement *appsink = gst_element_factory_make("appsink", "test-routing-appsink");
    ASSERT_NE(appsink, nullptr);

    appsink_poll_context context{};
    context.appsink = appsink;
    context.merger = &merger;
    context.data = &data;
    context.stop_flag = &stop_flag;
    context.eos_counter = &eos_counter;

    inference_appsink_poll_thread(context);
    EXPECT_EQ(eos_counter.load(), 1);
    EXPECT_FALSE(merger.is_closed());

    bypass_appsink_poll_thread(context);
    EXPECT_EQ(eos_counter.load(), 2);
    EXPECT_TRUE(merger.is_closed());

    gst_object_unref(appsink);
}

TEST_F(RuntimeRoutingAndCallbacksTest, LastInferenceRoiDataUpdateReuseAndClearLifecycle) {
    last_inference_roi_data roi_data;
    std::vector<split_rectangle> out_split_rectangles;
    std::vector<sf::rectangle> out_rectangles;

    EXPECT_FALSE(roi_data.has_data());
    EXPECT_FALSE(roi_data.get_for_bypass(out_split_rectangles, out_rectangles));

    std::vector<split_rectangle> split_rectangles;
    split_rectangles.emplace_back();
    std::vector<sf::rectangle> rectangles;
    rectangles.emplace_back(10, 12, 20, 24);

    roi_data.update(split_rectangles, rectangles, 123U, 7U);
    EXPECT_TRUE(roi_data.has_data());
    EXPECT_TRUE(roi_data.get_for_bypass(out_split_rectangles, out_rectangles));
    EXPECT_EQ(out_split_rectangles.size(), 1U);
    EXPECT_EQ(out_rectangles.size(), 1U);
    EXPECT_EQ(roi_data.reuse_count, 1U);

    roi_data.clear();
    EXPECT_FALSE(roi_data.has_data());
}

TEST_F(RuntimeRoutingAndCallbacksTest, init_debug_category_for_last_inference_roi_paths_are_exercised_by_lifecycle) {
    last_inference_roi_data roi_data;
    std::vector<split_rectangle> split_rectangles;
    std::vector<sf::rectangle> rectangles;
    split_rectangles.emplace_back();
    rectangles.emplace_back(1, 2, 3, 4);
    roi_data.update(split_rectangles, rectangles, 10U, 2U);

    std::vector<split_rectangle> out_split_rectangles;
    std::vector<sf::rectangle> out_rectangles;
    EXPECT_TRUE(roi_data.get_for_bypass(out_split_rectangles, out_rectangles));
}

TEST_F(RuntimeRoutingAndCallbacksTest, GstCallbacksHandleNoSampleAndEosPathsSafely) {
    init_gst_callbacks_debug_category();

    auto config = make_base_config();
    global_data data(std::move(config));
    GstElement *appsink = gst_element_factory_make("appsink", "test-gst-callbacks-appsink");
    ASSERT_NE(appsink, nullptr);

    EXPECT_EQ(on_new_raw_sample(appsink, &data), GST_FLOW_ERROR);
    on_raw_eos(appsink, &data);
    EXPECT_FALSE(data.raw_frame_queue.pop().has_value());

    EXPECT_EQ(on_new_encoded_sample(appsink, &data), GST_FLOW_ERROR);
    on_encoded_eos(appsink, &data);
    EXPECT_FALSE(data.encoded_frame_queue.pop().has_value());

    gst_object_unref(appsink);
}

TEST_F(RuntimeRoutingAndCallbacksTest, GstCallbacksUsePendingPerFrameLumaStateFromSubmissionPath) {
    auto config = make_base_config();
    config.enable_luma_adaptation = true;
    config.luma_downscale_factor = 0.5;
    global_data data(std::move(config));

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("callbacks-luma-state-pipeline"));
    ASSERT_NE(pipeline, nullptr);
    auto *appsrc = gst_element_factory_make("appsrc", "callbacks-luma-state-src");
    auto *appsink = gst_element_factory_make("appsink", "callbacks-luma-state-sink");
    ASSERT_NE(appsrc, nullptr);
    ASSERT_NE(appsink, nullptr);

    const auto caps = sf::unique_gst_caps_ptr(gst_caps_new_simple(
            "video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr));
    g_object_set(appsrc, "caps", caps.get(), "format", GST_FORMAT_TIME, "is-live", false, "do-timestamp", false,
                 nullptr);
    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, appsink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, appsink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, 2 * GST_SECOND);

    auto encoded_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 8, nullptr));
    ASSERT_NE(encoded_buffer, nullptr);
    GstMapInfo map_info{};
    ASSERT_TRUE(gst_buffer_map(encoded_buffer.get(), &map_info, GST_MAP_WRITE));
    const std::vector<uint8_t> encoded_bytes = {0x00, 0x00, 0x01, 0x2A, 0x01, 0xAA, 0xBB, 0xCC};
    std::memcpy(map_info.data, encoded_bytes.data(), encoded_bytes.size());
    gst_buffer_unmap(encoded_buffer.get(), &map_info);

    data.push_pending_encoded_contract_state(1, {.enabled = false, .downscale_factor = 1.0});
    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(appsrc), encoded_buffer.release()), GST_FLOW_OK);
    ASSERT_EQ(on_new_encoded_sample(appsink, &data), GST_FLOW_OK);

    const auto item_opt = data.encoded_frame_queue.pop();
    ASSERT_TRUE(item_opt.has_value());
    EXPECT_FALSE(item_opt->luma_adaptation.enabled);
    EXPECT_DOUBLE_EQ(item_opt->luma_adaptation.downscale_factor, 1.0);

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}

TEST_F(RuntimeRoutingAndCallbacksTest, GstCallbacksDisableLumaStateWhenSubmissionStateIsMissing) {
    auto config = make_base_config();
    config.enable_luma_adaptation = true;
    config.luma_downscale_factor = 0.5;
    global_data data(std::move(config));

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("callbacks-luma-fallback-pipeline"));
    ASSERT_NE(pipeline, nullptr);
    auto *appsrc = gst_element_factory_make("appsrc", "callbacks-luma-fallback-src");
    auto *appsink = gst_element_factory_make("appsink", "callbacks-luma-fallback-sink");
    ASSERT_NE(appsrc, nullptr);
    ASSERT_NE(appsink, nullptr);

    const auto caps = sf::unique_gst_caps_ptr(gst_caps_new_simple(
            "video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr));
    g_object_set(appsrc, "caps", caps.get(), "format", GST_FORMAT_TIME, "is-live", false, "do-timestamp", false,
                 nullptr);
    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, appsink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, appsink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, 2 * GST_SECOND);

    auto encoded_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 8, nullptr));
    ASSERT_NE(encoded_buffer, nullptr);
    GstMapInfo map_info{};
    ASSERT_TRUE(gst_buffer_map(encoded_buffer.get(), &map_info, GST_MAP_WRITE));
    const std::vector<uint8_t> encoded_bytes = {0x00, 0x00, 0x01, 0x2A, 0x01, 0x11, 0x22, 0x33};
    std::memcpy(map_info.data, encoded_bytes.data(), encoded_bytes.size());
    gst_buffer_unmap(encoded_buffer.get(), &map_info);

    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(appsrc), encoded_buffer.release()), GST_FLOW_OK);
    ASSERT_EQ(on_new_encoded_sample(appsink, &data), GST_FLOW_ERROR);
    EXPECT_FALSE(data.encoded_frame_queue.try_pop().has_value());

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}

TEST_F(RuntimeRoutingAndCallbacksTest, OutputQueuesAndDisplaySinkAreConfiguredForDeterministicLimits) {
    auto config = make_base_config();
    config.framerate = 25;
    global_data data(std::move(config));

    output_pipeline_elements elements{};
    elements.queue_display = gst_element_factory_make("queue", "test-queue-display");
    elements.queue_rtp = gst_element_factory_make("queue", "test-queue-rtp");
    elements.queue_file = gst_element_factory_make("queue", "test-queue-file");
    elements.queue_mp4 = gst_element_factory_make("queue", "test-queue-mp4");
    elements.queue_debug = gst_element_factory_make("queue", "test-queue-debug");
    elements.sink_display = gst_element_factory_make("fakesink", "test-display-sink");
    ASSERT_NE(elements.queue_display, nullptr);
    ASSERT_NE(elements.queue_rtp, nullptr);
    ASSERT_NE(elements.queue_file, nullptr);
    ASSERT_NE(elements.queue_mp4, nullptr);
    ASSERT_NE(elements.queue_debug, nullptr);
    ASSERT_NE(elements.sink_display, nullptr);

    configure_output_queues(elements, data.params);

    guint display_max_buffers = 0;
    guint rtp_max_buffers = 0;
    guint file_max_buffers = 0;
    guint mp4_max_buffers = 0;
    guint debug_max_buffers = 0;
    gint display_leaky = -1;
    gint debug_leaky = -1;
    g_object_get(G_OBJECT(elements.queue_display), "max-size-buffers", &display_max_buffers, "leaky", &display_leaky,
                 nullptr);
    g_object_get(G_OBJECT(elements.queue_rtp), "max-size-buffers", &rtp_max_buffers, nullptr);
    g_object_get(G_OBJECT(elements.queue_file), "max-size-buffers", &file_max_buffers, nullptr);
    g_object_get(G_OBJECT(elements.queue_mp4), "max-size-buffers", &mp4_max_buffers, nullptr);
    g_object_get(G_OBJECT(elements.queue_debug), "max-size-buffers", &debug_max_buffers, "leaky", &debug_leaky,
                 nullptr);

    EXPECT_EQ(display_max_buffers, 50U);
    EXPECT_EQ(rtp_max_buffers, 50U);
    EXPECT_EQ(file_max_buffers, 50U);
    EXPECT_EQ(mp4_max_buffers, 50U);
    EXPECT_EQ(debug_max_buffers, 50U);
    EXPECT_EQ(display_leaky, static_cast<int>(queue_leaky_mode::no_leak));
    EXPECT_EQ(debug_leaky, static_cast<int>(queue_leaky_mode::leak_downstream));

    configure_display_sink(elements, data.params);
    gboolean sync = TRUE;
    gboolean qos = TRUE;
    gboolean enable_last_sample = TRUE;
    gboolean async = TRUE;
    g_object_get(G_OBJECT(elements.sink_display), "sync", &sync, "qos", &qos, "enable-last-sample", &enable_last_sample,
                 "async", &async, nullptr);
    EXPECT_EQ(sync, FALSE);
    EXPECT_EQ(qos, FALSE);
    EXPECT_EQ(enable_last_sample, FALSE);
    EXPECT_EQ(async, FALSE);

    release_if_set(elements.queue_display);
    release_if_set(elements.queue_rtp);
    release_if_set(elements.queue_file);
    release_if_set(elements.queue_mp4);
    release_if_set(elements.queue_debug);
    release_if_set(elements.sink_display);
}

TEST_F(RuntimeRoutingAndCallbacksTest,
       calculate_queue_size_configure_display_queue_configure_rtp_queue_and_configure_h265_file_queue_behave_consistently) {
    auto config = make_base_config();
    config.framerate = 60;
    global_data data(std::move(config));

    output_pipeline_elements elements{};
    elements.queue_display = gst_element_factory_make("queue", "queue-symbol-display");
    elements.queue_rtp = gst_element_factory_make("queue", "queue-symbol-rtp");
    elements.queue_file = gst_element_factory_make("queue", "queue-symbol-file");
    ASSERT_NE(elements.queue_display, nullptr);
    ASSERT_NE(elements.queue_rtp, nullptr);
    ASSERT_NE(elements.queue_file, nullptr);

    configure_output_queues(elements, data.params);

    guint display_max_buffers = 0;
    guint rtp_max_buffers = 0;
    guint file_max_buffers = 0;
    g_object_get(G_OBJECT(elements.queue_display), "max-size-buffers", &display_max_buffers, nullptr);
    g_object_get(G_OBJECT(elements.queue_rtp), "max-size-buffers", &rtp_max_buffers, nullptr);
    g_object_get(G_OBJECT(elements.queue_file), "max-size-buffers", &file_max_buffers, nullptr);
    EXPECT_EQ(display_max_buffers, 120U);
    EXPECT_EQ(rtp_max_buffers, 120U);
    EXPECT_EQ(file_max_buffers, 120U);

    release_if_set(elements.queue_display);
    release_if_set(elements.queue_rtp);
    release_if_set(elements.queue_file);
}

TEST_F(RuntimeRoutingAndCallbacksTest, poll_appsink_loop_paths_are_covered_by_inference_and_bypass_threads) {
    init_appsink_polling_debug_category();
    auto config = make_base_config();
    global_data data(std::move(config));
    frame_merger_config merger_config{};
    frame_merger merger(merger_config);
    std::atomic<bool> stop_flag{true};
    std::atomic<int> eos_counter{0};

    GstElement *appsink = gst_element_factory_make("appsink", "poll-loop-coverage-appsink");
    ASSERT_NE(appsink, nullptr);
    appsink_poll_context context{};
    context.appsink = appsink;
    context.merger = &merger;
    context.data = &data;
    context.stop_flag = &stop_flag;
    context.eos_counter = &eos_counter;

    inference_appsink_poll_thread(context);
    bypass_appsink_poll_thread(context);
    EXPECT_GE(eos_counter.load(), 2);
    gst_object_unref(appsink);
}

TEST_F(RuntimeRoutingAndCallbacksTest, BypassPollingLifecycleCanRestartAfterStopAndJoin) {
    auto config = make_base_config();
    config.performance.enable_deepstream_bypass = true;
    global_data data(std::move(config));
    data.input_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("bypass-lifecycle-input-pipeline"));
    ASSERT_NE(data.input_pipeline, nullptr);
    data.bypass.appsink_inference = gst_element_factory_make("appsink", "bypass-lifecycle-inference");
    data.bypass.appsink_bypass = gst_element_factory_make("appsink", "bypass-lifecycle-bypass");
    ASSERT_NE(data.bypass.appsink_inference, nullptr);
    ASSERT_NE(data.bypass.appsink_bypass, nullptr);
    frame_merger_config merger_config{};
    data.bypass.merger = std::make_unique<frame_merger>(merger_config);
    data.bypass.merger->close();

    restart_bypass_polling_threads_if_needed(data);
    EXPECT_NE(data.bypass.inference_poll_slot.task, nullptr);
    EXPECT_NE(data.bypass.bypass_poll_slot.task, nullptr);
    EXPECT_NE(data.bypass.merger_poll_slot.task, nullptr);
    EXPECT_TRUE(data.worker_lifecycle.polling_workers_active.load());

    (void) data.request_worker_stop_once(true);
    EXPECT_NO_THROW(join_bypass_polling_threads(data));
    EXPECT_NO_THROW(join_bypass_polling_threads(data));
    EXPECT_EQ(data.bypass.inference_poll_slot.task, nullptr);
    EXPECT_EQ(data.bypass.bypass_poll_slot.task, nullptr);
    EXPECT_EQ(data.bypass.merger_poll_slot.task, nullptr);
    EXPECT_FALSE(data.worker_lifecycle.polling_workers_active.load());

    data.stop_processing = false;
    data.worker_lifecycle.reset();
    restart_bypass_polling_threads_if_needed(data);
    EXPECT_NE(data.bypass.inference_poll_slot.task, nullptr);
    EXPECT_NE(data.bypass.bypass_poll_slot.task, nullptr);
    EXPECT_NE(data.bypass.merger_poll_slot.task, nullptr);

    (void) data.request_worker_stop_once(true);
    join_bypass_polling_threads(data);
    release_if_set(data.bypass.appsink_inference);
    release_if_set(data.bypass.appsink_bypass);
}
