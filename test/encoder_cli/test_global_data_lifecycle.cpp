// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

/**
 * @file test_global_data_lifecycle.cpp
 * @brief Lifecycle and queue tests for global_data (appsrc/appsink report §10: EOS/restart/reset coverage).
 *
 * §10 mapping (encoder_cli):
 * - EOS/flush/restart-style behavior: `reset_processing_state`, `shutdown` idempotency, worker lifecycle.
 * - Per-instance output probe state reset: `ResetProcessingStateClearsOutputProbeRuntime` (multi-instance safety).
 */

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }

    [[nodiscard]] std::filesystem::path make_unique_temp_path(const std::string &suffix) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("global_data_lifecycle_" + std::to_string(now) + suffix);
    }
} // namespace

class GlobalDataLifecycleTest : public GStreamerTestFixture {};

TEST_F(GlobalDataLifecycleTest, HandleDisplayWindowClosedRequestsShutdownWhenNoOtherOutputsEnabled) {
    global_data data(make_base_config());
    EXPECT_FALSE(data.handle_display_window_closed());
    EXPECT_TRUE(data.display_window_closed.load());
}

TEST_F(GlobalDataLifecycleTest, HandleDisplayWindowClosedContinuesWhenAnotherOutputIsEnabled) {
    auto config = make_base_config();
    config.debug.save_frames = true;
    global_data data(std::move(config));

    EXPECT_TRUE(data.handle_display_window_closed());
    EXPECT_TRUE(data.display_window_closed.load());
}

TEST_F(GlobalDataLifecycleTest, ResetProcessingStateClearsOutputProbeRuntime) {
    global_data data(make_base_config());
    data.output_probe_runtime.random_access_timestamp_fix_frame_count = 11;
    data.output_probe_runtime.random_access_timestamp_base = 100;
    data.output_probe_runtime.display_decoder_input_log_frame_count = 22;
    data.output_probe_runtime.tee_display_queue_sink_log_frame_count = 33;

    data.reset_processing_state();

    EXPECT_EQ(data.output_probe_runtime.random_access_timestamp_fix_frame_count, 0);
    EXPECT_EQ(data.output_probe_runtime.random_access_timestamp_base, GST_CLOCK_TIME_NONE);
    EXPECT_EQ(data.output_probe_runtime.display_decoder_input_log_frame_count, 0);
    EXPECT_EQ(data.output_probe_runtime.tee_display_queue_sink_log_frame_count, 0);
}

TEST_F(GlobalDataLifecycleTest, ResetProcessingStateClearsQueuesAndCountersAndReopensQueues) {
    global_data data(make_base_config());

    data.raw_frame_queue.push(sf::unique_gst_sample_ptr{});
    data.encoded_frame_queue.push(make_encoded_frame_item(sf::unique_gst_sample_ptr{}));
    data.packed_regions_queue.push({.sequence_id = 1, .regions = sf::packed_regions_info::create_sentinel()});
    data.display_regions_queue.push({.sequence_id = 1, .regions = sf::packed_regions_info::create_sentinel()});
    data.debug_regions_queue.push({.sequence_id = 1, .regions = sf::packed_regions_info::create_sentinel()});

    data.num_input_frames = 42;
    data.expected_file_input_frames = 12;
    data.file_input_forced_raw_eos = true;
    data.current_frame_number.store(9);
    data.pending_qp_map_payload = qp_map_frame_payload{};
    data.shutdown_started = true;
    data.stop_processing = true;

    data.reset_processing_state();

    EXPECT_EQ(data.num_input_frames, 0U);
    EXPECT_FALSE(data.expected_file_input_frames.has_value());
    EXPECT_FALSE(data.file_input_forced_raw_eos.load());
    EXPECT_EQ(data.current_frame_number.load(), 0);
    EXPECT_FALSE(data.pending_qp_map_payload.has_value());
    EXPECT_FALSE(data.shutdown_started.load());
    EXPECT_FALSE(data.stop_processing.load());
    EXPECT_EQ(data.raw_frame_queue.size(), 0U);
    EXPECT_EQ(data.encoded_frame_queue.size(), 0U);
    EXPECT_EQ(data.packed_regions_queue.size(), 0U);
    EXPECT_EQ(data.display_regions_queue.size(), 0U);
    EXPECT_EQ(data.debug_regions_queue.size(), 0U);

    data.raw_frame_queue.push(sf::unique_gst_sample_ptr{});
    EXPECT_EQ(data.raw_frame_queue.size(), 1U);
}

TEST_F(GlobalDataLifecycleTest, ShutdownIsIdempotent) {
    global_data data(make_base_config());

    EXPECT_NO_THROW(data.shutdown());
    EXPECT_TRUE(data.shutdown_started.load());
    EXPECT_TRUE(data.stop_processing.load());

    EXPECT_NO_THROW(data.shutdown());
    EXPECT_TRUE(data.shutdown_started.load());
    EXPECT_TRUE(data.stop_processing.load());
}

TEST_F(GlobalDataLifecycleTest, WorkerLifecycleStopRequestIsIdempotentPerGeneration) {
    global_data data(make_base_config());
    data.begin_worker_lifecycle_cycle();

    EXPECT_EQ(data.worker_lifecycle.phase.load(), worker_lifecycle_phase::running);
    EXPECT_TRUE(data.request_worker_stop_once(true));
    EXPECT_TRUE(data.worker_lifecycle.stop_requested.load());
    EXPECT_EQ(data.worker_lifecycle.phase.load(), worker_lifecycle_phase::stopping);
    EXPECT_TRUE(data.stop_processing.load());
    EXPECT_FALSE(data.raw_frame_queue.pop().has_value());
    EXPECT_FALSE(data.encoded_frame_queue.pop().has_value());

    EXPECT_FALSE(data.request_worker_stop_once(true));
}

TEST_F(GlobalDataLifecycleTest, ResetProcessingStateResetsWorkerLifecycleForRestartCycles) {
    global_data data(make_base_config());
    data.begin_worker_lifecycle_cycle();
    data.mark_processing_workers_active(true);
    data.mark_message_workers_active(true);
    data.mark_polling_workers_active(true);
    ASSERT_TRUE(data.request_worker_stop_once(true));
    data.mark_worker_lifecycle_drained();

    EXPECT_EQ(data.worker_lifecycle.phase.load(), worker_lifecycle_phase::drained);
    data.reset_processing_state();
    EXPECT_EQ(data.worker_lifecycle.phase.load(), worker_lifecycle_phase::idle);
    EXPECT_FALSE(data.worker_lifecycle.stop_requested.load());
    EXPECT_FALSE(data.worker_lifecycle.processing_workers_active.load());
    EXPECT_FALSE(data.worker_lifecycle.message_workers_active.load());
    EXPECT_FALSE(data.worker_lifecycle.polling_workers_active.load());
}

TEST_F(GlobalDataLifecycleTest, ResetBridgeLifecycleStateResetsMemoryAndMetadataContractRuntimeCounters) {
    global_data data(make_base_config());

    data.memory_runtime.record_memory_domain_fallback_conversion();
    data.memory_runtime.record_writable_buffer_fallback_conversion();
    data.memory_runtime.record_fallback_failure();
    data.memory_runtime.roi_expected_domain.store(memory_domain_contract::nvmm);
    data.memory_runtime.encoding_upload_expected_domain.store(memory_domain_contract::cuda);
    data.metadata_runtime.record_bundle_enqueued();
    data.metadata_runtime.record_bundle_dequeued();
    data.metadata_runtime.record_validation_failure();
    data.metadata_runtime.record_fail_stop();
    data.metadata_runtime.expected_raw_sequence.store(11);
    data.metadata_runtime.expected_encoded_sequence.store(12);

    data.reset_bridge_lifecycle_state();

    const auto counters = data.memory_runtime.snapshot_counters();
    EXPECT_EQ(counters.memory_domain_fallback_conversions, 0U);
    EXPECT_EQ(counters.writable_buffer_fallback_conversions, 0U);
    EXPECT_EQ(counters.fallback_failures, 0U);
    EXPECT_EQ(data.memory_runtime.roi_expected_domain.load(), memory_domain_contract::system);
    EXPECT_EQ(data.memory_runtime.encoding_upload_expected_domain.load(), memory_domain_contract::unknown);

    const auto metadata_counters = data.metadata_runtime.snapshot_counters();
    EXPECT_EQ(metadata_counters.bundles_enqueued, 0U);
    EXPECT_EQ(metadata_counters.bundles_dequeued, 0U);
    EXPECT_EQ(metadata_counters.metadata_validation_failures, 0U);
    EXPECT_EQ(metadata_counters.contract_fail_stops, 0U);
    EXPECT_EQ(data.metadata_runtime.expected_raw_sequence.load(), 1U);
    EXPECT_EQ(data.metadata_runtime.expected_encoded_sequence.load(), 1U);
}

TEST_F(GlobalDataLifecycleTest, LoadStaticRoiDataSucceedsForValidFile) {
    auto config = make_base_config();
    const auto static_roi_path = make_unique_temp_path(".json");
    {
        std::ofstream output(static_roi_path);
        ASSERT_TRUE(output.is_open());
        output << R"({"rois":[{"x":10,"y":20,"width":30,"height":40}]})";
    }
    config.static_roi_json_file = static_roi_path.string();
    global_data data(std::move(config));

    ASSERT_TRUE(data.load_static_roi_data());
    ASSERT_TRUE(data.loaded_static_roi_data.has_value());
    EXPECT_EQ(data.loaded_static_roi_data->size(), 1U);
    std::filesystem::remove(static_roi_path);
}

TEST_F(GlobalDataLifecycleTest, LoadStaticRoiDataFailsForMissingFile) {
    auto config = make_base_config();
    config.static_roi_json_file = make_unique_temp_path(".missing.json").string();
    global_data data(std::move(config));

    EXPECT_FALSE(data.load_static_roi_data());
    EXPECT_FALSE(data.loaded_static_roi_data.has_value());
}

TEST_F(GlobalDataLifecycleTest, LoadGroundtruthRoiDataSucceedsForValidCocoFile) {
    auto config = make_base_config();
    const auto groundtruth_path = make_unique_temp_path(".groundtruth.json");
    {
        std::ofstream output(groundtruth_path);
        ASSERT_TRUE(output.is_open());
        output << R"({
  "images": [{ "id": 1, "file_name": "frame_000.png" }],
  "annotations": [{ "image_id": 1, "category_id": 3, "bbox": [1, 2, 3, 4] }]
})";
    }
    config.groundtruth_roi_file = groundtruth_path.string();
    global_data data(std::move(config));

    ASSERT_TRUE(data.load_groundtruth_roi_data());
    ASSERT_TRUE(data.loaded_groundtruth_roi_data.has_value());
    EXPECT_EQ(data.loaded_groundtruth_roi_data->size(), 1U);
    EXPECT_EQ(data.loaded_groundtruth_roi_data->total_annotations(), 1U);
    std::filesystem::remove(groundtruth_path);
}

TEST_F(GlobalDataLifecycleTest, LoadGroundtruthRoiDataFailsForMissingFile) {
    auto config = make_base_config();
    config.groundtruth_roi_file = make_unique_temp_path(".groundtruth.missing.json").string();
    global_data data(std::move(config));

    EXPECT_FALSE(data.load_groundtruth_roi_data());
    EXPECT_FALSE(data.loaded_groundtruth_roi_data.has_value());
}
