// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_drop_manager.h"
#include "../../encoder_cli/src/gst/input_types/input_ingress_facade.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::udp;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.udp.port = 5004;
        config.udp.encoding = "H264";
        config.udp.payload_type = 96U;
        config.udp.clock_rate = 90000U;
        config.udp.latency_ms = 100;
        return config;
    }

    void prime_display_fps(sf::pipeline_fps_monitor &monitor) {
        for (int i = 0; i < 10; ++i) {
            monitor.record_frame("display");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(550));
        monitor.record_frame("display");
    }
} // namespace

class FrameDropManagerTest : public GStreamerTestFixture {};

TEST_F(FrameDropManagerTest, NullBufferNeverDropsAndDoesNotCountFrame) {
    auto config = make_base_config();
    config.performance.enable_frame_dropping = true;
    global_data data(std::move(config));
    frame_drop_manager manager;
    const input_ingress_facade view(data);

    EXPECT_FALSE(manager.should_drop_frame(nullptr, view));
    const auto stats = manager.get_statistics();
    EXPECT_EQ(stats.total_frames_seen, 0U);
    EXPECT_EQ(stats.frames_dropped, 0U);
}

TEST_F(FrameDropManagerTest, DisabledModeNeverDropsButCountsSeenFrames) {
    auto config = make_base_config();
    config.performance.enable_frame_dropping = false;
    global_data data(std::move(config));
    frame_drop_manager manager;
    const input_ingress_facade view(data);

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 16, nullptr));
    ASSERT_NE(buffer.get(), nullptr);

    EXPECT_FALSE(manager.should_drop_frame(buffer.get(), view));
    const auto stats = manager.get_statistics();
    EXPECT_EQ(stats.total_frames_seen, 1U);
    EXPECT_EQ(stats.frames_dropped, 0U);
    EXPECT_DOUBLE_EQ(stats.drop_probability, 0.0);
}

TEST_F(FrameDropManagerTest, EnabledModeWithoutActivePipelinesDoesNotDrop) {
    auto config = make_base_config();
    config.performance.enable_frame_dropping = true;
    config.performance.target_fps = 60.0;
    global_data data(std::move(config));
    frame_drop_manager manager;
    const input_ingress_facade view(data);

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 16, nullptr));
    ASSERT_NE(buffer.get(), nullptr);

    EXPECT_FALSE(manager.should_drop_frame(buffer.get(), view));
    const auto stats = manager.get_statistics();
    EXPECT_EQ(stats.total_frames_seen, 1U);
    EXPECT_EQ(stats.frames_dropped, 0U);
}

TEST_F(FrameDropManagerTest, SustainedFpsDeficitTriggersDeterministicDrops) {
    auto config = make_base_config();
    config.performance.enable_frame_dropping = true;
    config.performance.target_fps = 120.0;
    config.performance.drop_smoothing_factor = 1.0;
    global_data data(std::move(config));
    frame_drop_manager manager;
    const input_ingress_facade view(data);

    prime_display_fps(data.monitoring.fps_monitor);
    EXPECT_TRUE(data.monitoring.fps_monitor.is_pipeline_active("display"));
    EXPECT_GT(data.monitoring.fps_monitor.get_current_fps("display"), 0.0);

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 16, nullptr));
    ASSERT_NE(buffer.get(), nullptr);

    int dropped = 0;
    for (int i = 0; i < 8; ++i) {
        if (manager.should_drop_frame(buffer.get(), view)) {
            dropped++;
        }
    }

    const auto stats = manager.get_statistics();
    EXPECT_GT(stats.smoothed_fps, 0.0);
    EXPECT_GT(stats.drop_probability, 0.0);
    EXPECT_GT(stats.frames_dropped, 0U);
    EXPECT_GT(dropped, 0);
}
