// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "libvcai/include/pipeline_fps_monitor.h"

TEST(PipelineFpsMonitorTest, ReturnsZeroForInsufficientFrames) {
    sf::pipeline_fps_monitor monitor;

    monitor.record_frame("display");

    EXPECT_EQ(monitor.get_current_fps("display"), 0.0);
    EXPECT_TRUE(monitor.is_pipeline_active("display"));
}

TEST(PipelineFpsMonitorTest, UpdatesAfterMinFramesAndInterval) {
    sf::pipeline_fps_monitor monitor;

    for (int index = 0; index < 10; ++index) {
        monitor.record_frame("display");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    monitor.record_frame("display");

    EXPECT_GT(monitor.get_current_fps("display"), 0.0);
}

TEST(PipelineFpsMonitorTest, BecomesInactiveAfterIdle) {
    sf::pipeline_fps_monitor monitor;

    monitor.record_frame("display");
    EXPECT_TRUE(monitor.is_pipeline_active("display"));

    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    EXPECT_FALSE(monitor.is_pipeline_active("display"));
}
