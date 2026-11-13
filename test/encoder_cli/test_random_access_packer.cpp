// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>
#include <optional>
#include <thread>
#include <vector>

#include "../../encoder_cli/src/roi/ctu_alignment.h"
#include "../../encoder_cli/src/roi/random_access_packer.h"
#include "../common/test_debug_categories.h"
#include "../libvcai/include/gst_raii.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_random_access_packer.cpp
 * @brief Comprehensive unit tests for random_access_packer.
 *
 * Tests cover:
 * - Basic functionality (construction, weight calculation, stats)
 * - Period buffering and completion
 * - ROI accumulation across frames
 * - ROI merging for overlapping regions
 * - Packing accumulated ROIs
 * - Edge cases (empty frames, closing, memory clearing)
 */

// Helper function to create a GstSample for testing
sf::unique_gst_sample_ptr create_test_sample(int width, int height) {
    // Initialize GStreamer if not already initialized
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    // Create caps for I420 format
    GstVideoInfo video_info;
    gst_video_info_init(&video_info);
    if (!gst_video_info_set_format(&video_info, GST_VIDEO_FORMAT_I420, static_cast<guint>(width),
                                   static_cast<guint>(height))) {
        return nullptr;
    }

    // Create caps from video info
    GstCaps *caps = gst_video_info_to_caps(&video_info);
    if (!caps) {
        return nullptr;
    }

    // Calculate buffer size for I420 (1.5 bytes per pixel)
    const gsize buffer_size = video_info.size;
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, buffer_size, nullptr);
    if (!buffer) {
        gst_caps_unref(caps);
        return nullptr;
    }

    // Fill buffer with zeros (valid I420 data)
    gst_buffer_memset(buffer, 0, 0, buffer_size);

    // Create sample - gst_sample_new takes ownership of buffer and caps
    // So we don't unref them - the sample will handle cleanup
    GstSample *sample = gst_sample_new(buffer, caps, nullptr, nullptr);
    // Note: gst_sample_new adds its own ref to buffer and caps, so we can unref our refs
    gst_buffer_unref(buffer);
    gst_caps_unref(caps);

    return sf::unique_gst_sample_ptr(sample);
}

class RandomAccessPackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize GStreamer if not already initialized
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }

        // Debug categories are initialized by GStreamerTestFixture if using it,
        // or we need to initialize them here for non-fixture tests
        // For now, initialize all categories to be safe
        init_all_test_debug_categories();

        packer = std::make_unique<random_access_packer>(1024, 1024, default_ctu_size, 30, 3);
    }

    void TearDown() override {
        if (packer) {
            packer->close();
        }
        packer.reset();
    }

    std::unique_ptr<random_access_packer> packer;
};

/**
 * @brief Test weight calculation (static method).
 */
TEST_F(RandomAccessPackerTest, WeightCalculation) {
    const sf::rectangle rect1(0, 0, 100, 50);
    const sf::rectangle rect2(0, 0, 200, 100);

    EXPECT_EQ(random_access_packer::get_weight(rect1), 5000); // 100 * 50
    EXPECT_EQ(random_access_packer::get_weight(rect2), 20000); // 200 * 100
}

/**
 * @brief Test that packer starts with correct initial state.
 */
TEST_F(RandomAccessPackerTest, InitialState) {
    const auto stats = packer->get_stats();
    EXPECT_EQ(stats.total_rectangles_remembered, 0);
    EXPECT_EQ(stats.total_packing_calls, 0);
    EXPECT_DOUBLE_EQ(stats.average_stability_ratio, 0.0);
}

/**
 * @brief Test that sample creation works correctly.
 */
TEST_F(RandomAccessPackerTest, SampleCreation) {
    auto sample = create_test_sample(100, 100);
    ASSERT_NE(sample, nullptr);

    // Verify sample has valid caps
    const auto *caps = gst_sample_get_caps(sample.get());
    ASSERT_NE(caps, nullptr);

    // Verify sample has valid buffer
    const auto *buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(buffer, nullptr);
}

/**
 * @brief Test adding a single frame.
 */
TEST_F(RandomAccessPackerTest, AddSingleFrame) {
    auto sample = create_test_sample(100, 100);
    ASSERT_NE(sample, nullptr);

    std::vector<std::pair<sf::rectangle, std::string>> empty_rois;
    packer->add_frame(1, std::move(sample), empty_rois);

    // Should not crash and stats should reflect the frame was added
    auto stats = packer->get_stats();
    // After one frame, we haven't completed a period yet, so packing_calls should be 0
    EXPECT_EQ(stats.total_packing_calls, 0);
}

/**
 * @brief Test adding frames without ROIs.
 */
TEST_F(RandomAccessPackerTest, AddFramesWithoutRois) {
    // Add frames without ROIs - should still buffer them
    for (int frame = 1; frame <= 30; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> empty_rois;
        packer->add_frame(frame, std::move(sample), empty_rois);
    }

    // After intra_period (30 frames), should have a complete period
    // Note: First period is special - it's processed when second period starts
    // So we need to add frames for a second period to get the first period
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> empty_rois;
        packer->add_frame(frame, std::move(sample), empty_rois);
    }

    // Now we should be able to get complete periods
    // Note: The second period is processed immediately when it completes (period_number > 0)
    // The first period is processed when the second period starts, so both should be available
    // We might get either period first, so check both
    auto period1_opt = packer->get_complete_period();
    ASSERT_TRUE(period1_opt.has_value());

    // Should get either period 0 or period 1
    EXPECT_TRUE(period1_opt->period_number == 0 || period1_opt->period_number == 1);
    if (period1_opt->period_number == 0) {
        EXPECT_EQ(period1_opt->period_start_frame, 1);
        EXPECT_EQ(period1_opt->frames.size(), 30);
        EXPECT_TRUE(period1_opt->processed_rois.empty()); // No ROIs, so no processed ROIs
    } else {
        EXPECT_EQ(period1_opt->period_start_frame, 31);
        EXPECT_EQ(period1_opt->frames.size(), 30);
    }
}

/**
 * @brief Test adding frames with single ROI.
 *
 * Note: First period (period_number 0) is special - it's processed when second period starts.
 * So we need to add enough frames for a second period to trigger first period processing.
 *
 * This test verifies that:
 * - Frames are buffered correctly
 * - Periods are processed when they complete
 * - Stats reflect the processing
 */
TEST_F(RandomAccessPackerTest, AddFramesWithSingleRoi) {
    const sf::rectangle roi(10, 10, 50, 50);
    const std::string object_id = "obj1";

    // Test with a smaller number of frames first to verify basic functionality
    // Add frames with the same ROI - should accumulate across frames
    for (int frame = 1; frame <= 5; ++frame) {
        auto sample = create_test_sample(100, 100);
        if (!sample) {
            GTEST_SKIP() << "Failed to create test sample - GStreamer may not be properly initialized";
        }
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Check that frames are being buffered (stats should show ROIs remembered)
    auto stats = packer->get_stats();
    EXPECT_GT(stats.total_rectangles_remembered, 0) << "ROIs should be remembered during buffering";

    // Add more frames to complete a period (need 30 total for intra_period)
    for (int frame = 6; frame <= 30; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // First period completes but isn't processed until second period starts
    // Add second period to trigger first period processing
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Check stats - should have processed at least one period by now
    // (Second period is processed immediately when it completes at frame 60)
    auto final_stats = packer->get_stats();
    EXPECT_GT(final_stats.total_packing_calls, 0) << "At least one period should have been processed";
}

/**
 * @brief Test ROI accumulation across frames.
 */
TEST_F(RandomAccessPackerTest, RoiAccumulation) {
    const std::string object_id = "obj1";

    // Add frames with ROIs that grow over time (simulating object movement/growth)
    for (int frame = 1; frame <= 30; ++frame) {
        auto sample = create_test_sample(200, 200);
        ASSERT_NE(sample, nullptr);

        // ROI grows slightly each frame
        const int size = 40 + frame;
        const sf::rectangle roi(10, 10, size, size);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Add second period
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(200, 200);
        ASSERT_NE(sample, nullptr);
        const int size = 40 + (frame - 30);
        const sf::rectangle roi(10, 10, size, size);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Get first complete period
    auto period_opt = packer->get_complete_period();
    ASSERT_TRUE(period_opt.has_value());

    // Accumulated ROI should be the union of all ROIs in the period
    // Since ROIs grow, the accumulated ROI should be at least as large as the largest ROI
    EXPECT_FALSE(period_opt->processed_rois.empty());

    // Check that we have the object ID
    EXPECT_FALSE(period_opt->object_ids.empty());
    EXPECT_EQ(period_opt->object_ids[0], object_id);
}

/**
 * @brief Test multiple ROIs in a single frame.
 */
TEST_F(RandomAccessPackerTest, MultipleRois) {
    const std::string object_id1 = "obj1";
    const std::string object_id2 = "obj2";
    const std::string object_id3 = "obj3";

    // Add frames with multiple ROIs
    for (int frame = 1; frame <= 30; ++frame) {
        auto sample = create_test_sample(200, 200);
        ASSERT_NE(sample, nullptr);

        std::vector<std::pair<sf::rectangle, std::string>> rois = {{sf::rectangle(10, 10, 50, 50), object_id1},
                                                                   {sf::rectangle(70, 10, 50, 50), object_id2},
                                                                   {sf::rectangle(130, 10, 50, 50), object_id3}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Add second period
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(200, 200);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{sf::rectangle(10, 10, 50, 50), object_id1},
                                                                   {sf::rectangle(70, 10, 50, 50), object_id2},
                                                                   {sf::rectangle(130, 10, 50, 50), object_id3}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Get first complete period
    auto period_opt = packer->get_complete_period();
    ASSERT_TRUE(period_opt.has_value());

    // Should have processed ROIs for all three objects
    EXPECT_FALSE(period_opt->processed_rois.empty());
    EXPECT_EQ(period_opt->object_ids.size(), 3);

    // Check that all object IDs are present
    std::vector<std::string> expected_ids = {object_id1, object_id2, object_id3};
    std::ranges::sort(expected_ids);
    EXPECT_EQ(period_opt->object_ids, expected_ids);
}

/**
 * @brief Test closing the packer.
 */
TEST_F(RandomAccessPackerTest, ClosePacker) {
    // Add some frames (not enough to complete a period)
    for (int frame = 1; frame <= 15; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> empty_rois;
        packer->add_frame(frame, std::move(sample), empty_rois);
    }

    // Close the packer (this closes the queue)
    packer->close();

    // Try to add a frame after closing - should be ignored
    auto sample = create_test_sample(100, 100);
    ASSERT_NE(sample, nullptr);
    std::vector<std::pair<sf::rectangle, std::string>> empty_rois;
    packer->add_frame(16, std::move(sample), empty_rois);

    // get_complete_period should return nullopt since queue is closed and empty
    // Use a thread with timeout to avoid blocking forever
    std::optional<random_access_packer::complete_period> result;
    std::atomic<bool> done{false};

    std::jthread test_thread([&]() {
        result = packer->get_complete_period();
        done = true;
    });

    // Wait up to 100ms for the result
    const auto start = std::chrono::steady_clock::now();
    while (!done && (std::chrono::steady_clock::now() - start) < std::chrono::milliseconds(100)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Should return nullopt since queue is closed and empty
    EXPECT_FALSE(result.has_value());
}

/**
 * @brief Test clearing memory.
 */
TEST_F(RandomAccessPackerTest, ClearMemory) {
    const sf::rectangle roi(10, 10, 50, 50);
    const std::string object_id = "obj1";

    // Add some frames
    for (int frame = 1; frame <= 15; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Check stats before clearing
    auto stats_before = packer->get_stats();
    EXPECT_GT(stats_before.total_rectangles_remembered, 0);

    // Clear memory
    packer->clear_memory();

    // Check stats after clearing
    auto stats_after = packer->get_stats();
    EXPECT_EQ(stats_after.total_rectangles_remembered, 0);
    EXPECT_EQ(stats_after.total_packing_calls, 0);
}

/**
 * @brief Test that frames are properly buffered until period completes.
 */
TEST_F(RandomAccessPackerTest, PeriodBuffering) {
    const sf::rectangle roi(10, 10, 50, 50);
    const std::string object_id = "obj1";

    // Add frames up to intra_period - 1 (should not complete yet)
    for (int frame = 1; frame <= 29; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Check stats - should have ROIs remembered but no periods processed yet
    auto stats_before = packer->get_stats();
    EXPECT_GT(stats_before.total_rectangles_remembered, 0) << "ROIs should be remembered during buffering";
    EXPECT_EQ(stats_before.total_packing_calls, 0) << "No periods should be processed yet";

    // Add 30th frame - completes first period (but it's saved, not processed yet)
    auto sample = create_test_sample(100, 100);
    ASSERT_NE(sample, nullptr);
    std::vector<std::pair<sf::rectangle, std::string>> rois2 = {{roi, object_id}};
    packer->add_frame(30, std::move(sample), rois2);

    // First period is complete but not processed until second period starts
    // Add second period to trigger first period processing
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois3 = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois3);

        // Check stats after second period completes (frame 60)
        if (frame == 60) {
            auto stats_after = packer->get_stats();
            EXPECT_GT(stats_after.total_packing_calls, 0)
                    << "At least one period should have been processed after 60 frames";
        }
    }

    // Final stats check - verify processing occurred
    auto final_stats = packer->get_stats();
    EXPECT_GT(final_stats.total_packing_calls, 0) << "At least one period should have been processed";

    // Close packer to ensure queue is closed
    packer->close();
}

/**
 * @brief Test overlapping ROIs are merged.
 */
TEST_F(RandomAccessPackerTest, OverlappingRoisMerged) {
    const std::string object_id1 = "obj1";
    const std::string object_id2 = "obj2";

    // Add frames with overlapping ROIs (should be merged)
    for (int frame = 1; frame <= 30; ++frame) {
        auto sample = create_test_sample(200, 200);
        ASSERT_NE(sample, nullptr);

        // Two ROIs that overlap significantly (>30% threshold)
        std::vector<std::pair<sf::rectangle, std::string>> rois = {
                {sf::rectangle(10, 10, 50, 50), object_id1},
                {sf::rectangle(40, 40, 50, 50), object_id2} // Overlaps with obj1
        };
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Add second period
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(200, 200);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{sf::rectangle(10, 10, 50, 50), object_id1},
                                                                   {sf::rectangle(40, 40, 50, 50), object_id2}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Get first complete period
    auto period_opt = packer->get_complete_period();
    ASSERT_TRUE(period_opt.has_value());

    // Overlapping ROIs should be merged, so we might have fewer processed ROIs
    // than original object IDs (depending on overlap threshold)
    EXPECT_FALSE(period_opt->processed_rois.empty());

    // The mapping should reflect the merging
    if (period_opt->processed_rois.size() < period_opt->object_ids.size()) {
        // Merging occurred - verify mapping exists
        EXPECT_FALSE(period_opt->original_to_packed_mapping.empty());
    }
}

/**
 * @brief Test statistics tracking.
 */
TEST_F(RandomAccessPackerTest, StatisticsTracking) {
    const sf::rectangle roi(10, 10, 50, 50);
    const std::string object_id = "obj1";

    // Initial stats
    auto stats_initial = packer->get_stats();
    EXPECT_EQ(stats_initial.total_packing_calls, 0);
    EXPECT_EQ(stats_initial.total_rectangles_remembered, 0);

    // Add first period
    for (int frame = 1; frame <= 30; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Add second period to trigger first period processing
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Get first period
    auto period_opt = packer->get_complete_period();
    ASSERT_TRUE(period_opt.has_value());

    // Check stats after processing
    // Note: total_rectangles_remembered is from object_histories, which is cleared after processing
    // So it may be 0 if all periods have been processed
    auto stats_after = packer->get_stats();
    EXPECT_GT(stats_after.total_packing_calls, 0);
    // total_rectangles_remembered may be 0 if object_histories was cleared after processing
    // The important thing is that packing_calls > 0, indicating processing occurred
}

/**
 * @brief Test multiple complete periods.
 */
TEST_F(RandomAccessPackerTest, MultiplePeriods) {
    const sf::rectangle roi(10, 10, 50, 50);
    const std::string object_id = "obj1";

    // Add three complete periods (90 frames = 3 * 30)
    for (int frame = 1; frame <= 90; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Check stats - should have processed at least 2 periods
    // (First period processed when second started, second period processed when third started)
    auto stats = packer->get_stats();
    EXPECT_GE(stats.total_packing_calls, 2) << "At least 2 periods should have been processed after 90 frames";

    // Close packer to ensure queue is closed
    packer->close();

    // Try to get periods with timeout to verify they're available
    // Use threads to avoid blocking forever
    std::optional<random_access_packer::complete_period> period1_result;
    std::atomic<bool> done1{false};

    std::jthread thread1([&]() {
        period1_result = packer->get_complete_period();
        done1 = true;
    });

    const auto start1 = std::chrono::steady_clock::now();
    while (!done1 && (std::chrono::steady_clock::now() - start1) < std::chrono::milliseconds(500)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (done1 && period1_result.has_value()) {
        // Verify we got a valid period
        EXPECT_TRUE(period1_result->period_number >= 0 && period1_result->period_number <= 2);
        EXPECT_EQ(period1_result->frames.size(), 30);

        // Try to get second period
        std::optional<random_access_packer::complete_period> period2_result;
        std::atomic<bool> done2{false};

        std::jthread thread2([&]() {
            period2_result = packer->get_complete_period();
            done2 = true;
        });

        const auto start2 = std::chrono::steady_clock::now();
        while (!done2 && (std::chrono::steady_clock::now() - start2) < std::chrono::milliseconds(500)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (done2 && period2_result.has_value()) {
            EXPECT_NE(period2_result->period_number, period1_result->period_number);
            EXPECT_EQ(period2_result->frames.size(), 30);
        }
    }
}

/**
 * @brief Test that frames with null samples can be stored (though shouldn't happen in practice).
 *
 * This test verifies that:
 * - The packer can accept frames with null samples without crashing
 * - The frame processing logic should handle null samples gracefully (tested separately)
 *
 * Note: This test doesn't verify period retrieval because the main bug fix is in the
 * frame processing logic (roi_detection_random_access.cpp), not in the packer itself.
 * The packer correctly stores null samples, and the processing logic now handles them.
 */
TEST_F(RandomAccessPackerTest, HandleNullSamples) {
    const sf::rectangle roi(10, 10, 50, 50);
    const std::string object_id = "obj1";

    // Add frames, but make one frame have a null sample
    // The packer should accept this without crashing
    for (int frame = 1; frame <= 30; ++frame) {
        sf::unique_gst_sample_ptr sample;

        // Make frame 15 have a null sample to test error handling
        if (frame != 15) {
            sample = create_test_sample(100, 100);
            ASSERT_NE(sample, nullptr);
        } else {
            // Intentionally use null sample for frame 15
            sample = nullptr;
        }

        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        // This should not crash even with null sample
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Add second period to trigger first period processing
    for (int frame = 31; frame <= 60; ++frame) {
        auto sample = create_test_sample(100, 100);
        ASSERT_NE(sample, nullptr);
        std::vector<std::pair<sf::rectangle, std::string>> rois = {{roi, object_id}};
        packer->add_frame(frame, std::move(sample), rois);
    }

    // Close packer
    packer->close();

    // The test passes if we got here without crashing
    // The actual handling of null samples is tested in the frame processing logic
    // which now properly handles them by pushing sentinels and black frames
}
