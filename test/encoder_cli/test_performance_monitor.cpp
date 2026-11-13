// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define private public
#include "../../encoder_cli/src/reporting/performance_monitor.h"
#undef private

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
} // namespace

class PerformanceMonitorTest : public GStreamerTestFixture {};

TEST_F(PerformanceMonitorTest, GetStageNameCoversExpectedStages) {
    EXPECT_STREQ(get_stage_name(monitoring_stage::inference), "Inference");
    EXPECT_STREQ(get_stage_name(monitoring_stage::inference_streammux), "Inference Streammux");
    EXPECT_STREQ(get_stage_name(monitoring_stage::inference_nvinfer), "Inference Nvinfer");
    EXPECT_STREQ(get_stage_name(monitoring_stage::inference_tracker), "Inference Tracker");
    EXPECT_STREQ(get_stage_name(monitoring_stage::inference_conversion), "Inference Conversion");
    EXPECT_STREQ(get_stage_name(monitoring_stage::roi_detection_processing), "ROI Detection Processing");
    EXPECT_STREQ(get_stage_name(monitoring_stage::encoding), "Encoding");
    EXPECT_STREQ(get_stage_name(monitoring_stage::encoded_frame_processing), "Encoded Frame Processing");
    EXPECT_STREQ(get_stage_name(monitoring_stage::input_decoding), "Input Decoding");
    EXPECT_STREQ(get_stage_name(monitoring_stage::total_pipeline), "Total Pipeline");
}

TEST_F(PerformanceMonitorTest, RecordTimingUpdatesCountersAndCapsRecentWindow) {
    global_data data(make_base_config());
    performance_monitor monitor(data.params, data.ingress_runtime, data.memory_runtime, data.metadata_runtime,
                                data.stop_processing, data.udp_input_runtime);

    constexpr int sample_count = 1101;
    for (int sample_index = 0; sample_index < sample_count; ++sample_index) {
        monitor.record_timing(monitoring_stage::encoding, static_cast<uint64_t>(sample_index + 1));
    }

    auto &stats = monitor.stage_stats[static_cast<size_t>(monitoring_stage::encoding)];
    EXPECT_EQ(stats.frame_count.load(), static_cast<uint64_t>(sample_count));
    EXPECT_EQ(stats.min_time_ns.load(), 1U);
    EXPECT_EQ(stats.max_time_ns.load(), static_cast<uint64_t>(sample_count));

    std::lock_guard<std::mutex> lock(stats.recent_times_mutex);
    EXPECT_EQ(stats.recent_times.size(), timing_stats::recent_window_size);
    ASSERT_FALSE(stats.recent_times.empty());
    EXPECT_EQ(stats.recent_times.front(), 102U);
    EXPECT_EQ(stats.recent_times.back(), static_cast<uint64_t>(sample_count));
}

TEST_F(PerformanceMonitorTest, CalculateRecentStatsHandlesEmptyAndSingleSamples) {
    global_data data(make_base_config());
    performance_monitor monitor(data.params, data.ingress_runtime, data.memory_runtime, data.metadata_runtime,
                                data.stop_processing, data.udp_input_runtime);

    const auto empty_stats = monitor.calculate_recent_stats(monitoring_stage::inference);
    EXPECT_DOUBLE_EQ(empty_stats.p50_ns, 0.0);
    EXPECT_DOUBLE_EQ(empty_stats.p95_ns, 0.0);
    EXPECT_DOUBLE_EQ(empty_stats.p99_ns, 0.0);

    monitor.record_timing(monitoring_stage::inference, 42U);
    const auto one_sample_stats = monitor.calculate_recent_stats(monitoring_stage::inference);
    EXPECT_DOUBLE_EQ(one_sample_stats.p50_ns, 42.0);
    EXPECT_DOUBLE_EQ(one_sample_stats.p95_ns, 42.0);
    EXPECT_DOUBLE_EQ(one_sample_stats.p99_ns, 42.0);
}

TEST_F(PerformanceMonitorTest, RecordStartEndTracksAndConsumesPtsEntry) {
    global_data data(make_base_config());
    performance_monitor monitor(data.params, data.ingress_runtime, data.memory_runtime, data.metadata_runtime,
                                data.stop_processing, data.udp_input_runtime);

    constexpr GstClockTime pts = 123456789U;
    monitor.record_start_time(monitoring_stage::total_pipeline, pts);

    EXPECT_EQ(monitor.total_pipeline_start_times.size(), 1U);
    EXPECT_NE(monitor.total_pipeline_start_times.find(pts), monitor.total_pipeline_start_times.end());

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    monitor.record_end_time(monitoring_stage::total_pipeline, pts);

    EXPECT_EQ(monitor.total_pipeline_start_times.size(), 0U);
    const auto &stats = monitor.stage_stats[static_cast<size_t>(monitoring_stage::total_pipeline)];
    EXPECT_EQ(stats.frame_count.load(), 1U);
}

TEST_F(PerformanceMonitorTest, CalculateIntervalStatsUsesCurrentBaseline) {
    global_data data(make_base_config());
    performance_monitor monitor(data.params, data.ingress_runtime, data.memory_runtime, data.metadata_runtime,
                                data.stop_processing, data.udp_input_runtime);

    constexpr auto stage = monitoring_stage::encoding;
    monitor.record_timing(stage, 1000U);
    monitor.record_timing(stage, 3000U);

    auto &stats = monitor.stage_stats[static_cast<size_t>(stage)];
    stats.last_interval_frame_count = 0;
    stats.last_interval_total_time_ns = 0;
    stats.last_report_time = std::chrono::steady_clock::now() - std::chrono::seconds(2);

    const auto interval = monitor.calculate_interval_stats(stage);
    EXPECT_EQ(interval.interval_frames, 2U);
    EXPECT_GT(interval.interval_avg_ns, 0.0);
    EXPECT_GT(interval.interval_fps, 0.0);
}
