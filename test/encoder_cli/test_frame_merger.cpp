// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "../../encoder_cli/src/gst/input_types/frame_merger.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] sf::unique_gst_sample_ptr make_sample_with_pts(const GstClockTime pts) {
        auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 16, nullptr));
        if (!buffer) {
            return nullptr;
        }
        GST_BUFFER_PTS(buffer.get()) = pts;

        auto caps = sf::unique_gst_caps_ptr(gst_caps_new_empty_simple("application/octet-stream"));
        if (!caps) {
            return nullptr;
        }

        GstSample *sample = gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr);
        return sf::unique_gst_sample_ptr(sample);
    }
} // namespace

class FrameMergerTest : public GStreamerTestFixture {};

TEST_F(FrameMergerTest, TimeoutPathReleasesOldestFrameWhenSinglePathContinues) {
    frame_merger_config config{};
    config.max_buffer_frames = 8;
    config.max_latency_ms = 20;
    config.emergency_drop_threshold = 16;
    frame_merger merger(config);

    std::optional<merged_frame> result;
    std::atomic<bool> done{false};
    std::jthread worker([&]() {
        result = merger.get_next_frame();
        done.store(true);
    });

    merger.add_inference_frame(make_sample_with_pts(100));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(done.load()) << "Frame should wait briefly for the other path.";

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    merger.add_inference_frame(make_sample_with_pts(200)); // wakes waiter and allows timeout predicate to pass

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!done.load()) {
        merger.close();
    }
    ASSERT_TRUE(done.load());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->pts, 100U);
    EXPECT_EQ(result->path, frame_path_type::inference);

    const auto stats = merger.get_statistics();
    EXPECT_GE(stats.timeout_events, 1U);
}

TEST_F(FrameMergerTest, ReorderedPtsIncrementsReorderStatistics) {
    frame_merger_config config{};
    config.max_buffer_frames = 8;
    config.max_latency_ms = 0;
    config.emergency_drop_threshold = 16;
    frame_merger merger(config);

    merger.add_inference_frame(make_sample_with_pts(200));
    const auto first = merger.get_next_frame();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->pts, 200U);

    merger.add_inference_frame(make_sample_with_pts(100));
    const auto second = merger.get_next_frame();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->pts, 100U);

    const auto stats = merger.get_statistics();
    EXPECT_EQ(stats.reorder_events, 1U);
    EXPECT_EQ(stats.frames_output, 2U);
}

TEST_F(FrameMergerTest, CloseDrainsBufferedFramesThenReturnsNullopt) {
    frame_merger_config config{};
    config.max_buffer_frames = 8;
    config.max_latency_ms = 100;
    config.emergency_drop_threshold = 16;
    frame_merger merger(config);

    merger.add_inference_frame(make_sample_with_pts(200));
    merger.add_bypass_frame(make_sample_with_pts(100));
    merger.close();
    EXPECT_TRUE(merger.is_closed());

    const auto first = merger.get_next_frame();
    const auto second = merger.get_next_frame();
    const auto third = merger.get_next_frame();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(third.has_value());
    EXPECT_EQ(first->pts, 100U);
    EXPECT_EQ(second->pts, 200U);

    const auto stats = merger.get_statistics();
    EXPECT_EQ(stats.frames_output, 2U);
    EXPECT_EQ(stats.frames_from_inference, 1U);
    EXPECT_EQ(stats.frames_from_bypass, 1U);
}

TEST_F(FrameMergerTest, RequestConsumerShutdownUnblocksEmptyWait) {
    frame_merger_config config{};
    config.max_latency_ms = 5000;
    frame_merger merger(config);

    std::optional<merged_frame> result;
    std::atomic<bool> done{false};
    std::jthread waiter([&]() {
        result = merger.get_next_frame();
        done.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(done.load());

    merger.request_consumer_shutdown();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(done.load());
    EXPECT_FALSE(result.has_value());
}
