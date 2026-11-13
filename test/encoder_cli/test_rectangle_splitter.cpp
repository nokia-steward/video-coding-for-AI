// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>
#include <vector>

#include "../../encoder_cli/src/processing/roi_detection_processing.h"
#include "../../encoder_cli/src/roi/rectangle_splitter.h"
#include "../../encoder_cli/src/roi/rectangle_splitter/splitting.h"
#include "../../encoder_cli/src/roi/roi_manager.h"
#include "../common/test_debug_categories.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_rectangle_splitter.cpp
 * @brief Comprehensive tests for rectangle splitting functionality.
 */

class RectangleSplitterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize GStreamer if not already initialized
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
        // Initialize all debug categories (required for code under test to work correctly)
        init_all_test_debug_categories();
        frame_width = 1920;
        frame_height = 1080;
    }

    void TearDown() override {}

    int frame_width;
    int frame_height;
};

/**
 * @brief Test splitting with no overlaps (should return original rectangles).
 */
TEST_F(RectangleSplitterTest, NoOverlaps) {
    std::vector<detected_object> tracked_objects;

    // Create two non-overlapping objects
    detected_object obj1(1);
    object_data data1(sf::rectangle{100, 100, 50, 50}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    detected_object obj2(2);
    object_data data2(sf::rectangle{200, 200, 50, 50}, 0.8f, 1, sf::yuv_color::blue);
    obj2.history.push_back(data2);
    tracked_objects.push_back(obj2);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    ASSERT_EQ(result.size(), 2);
    EXPECT_FALSE(result[0].was_split);
    EXPECT_FALSE(result[1].was_split);
    EXPECT_EQ(result[0].bounds.left, 100);
    EXPECT_EQ(result[0].bounds.top, 100);
    EXPECT_EQ(result[1].bounds.left, 200);
    EXPECT_EQ(result[1].bounds.top, 200);
}

/**
 * @brief Test splitting with two overlapping rectangles.
 */
TEST_F(RectangleSplitterTest, TwoOverlappingRectangles) {
    std::vector<detected_object> tracked_objects;

    // Create two overlapping objects
    detected_object obj1(1);
    object_data data1(sf::rectangle{100, 100, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    detected_object obj2(2);
    object_data data2(sf::rectangle{150, 150, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj2.history.push_back(data2);
    tracked_objects.push_back(obj2);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    // At least one rectangle should be split
    ASSERT_GE(result.size(), 2);

    // Verify all rectangles have even dimensions
    for (const auto &split_rect: result) {
        EXPECT_EQ(split_rect.bounds.width % 2, 0);
        EXPECT_EQ(split_rect.bounds.height % 2, 0);
        EXPECT_EQ(split_rect.bounds.left % 2, 0);
        EXPECT_EQ(split_rect.bounds.top % 2, 0);
    }
}

/**
 * @brief Test splitting with single rectangle (should not split).
 */
TEST_F(RectangleSplitterTest, SingleRectangle) {
    std::vector<detected_object> tracked_objects;

    detected_object obj1(1);
    object_data data1(sf::rectangle{100, 100, 50, 50}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    ASSERT_EQ(result.size(), 1);
    EXPECT_FALSE(result[0].was_split);
    EXPECT_EQ(result[0].bounds.left, 100);
    EXPECT_EQ(result[0].bounds.top, 100);
}

/**
 * @brief Test splitting with completely contained rectangle.
 */
TEST_F(RectangleSplitterTest, ContainedRectangle) {
    std::vector<detected_object> tracked_objects;

    // Large rectangle containing a smaller one
    detected_object obj1(1);
    object_data data1(sf::rectangle{100, 100, 200, 200}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    detected_object obj2(2);
    object_data data2(sf::rectangle{150, 150, 50, 50}, 0.8f, 1, sf::yuv_color::blue);
    obj2.history.push_back(data2);
    tracked_objects.push_back(obj2);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    // The splitting algorithm behavior with contained rectangles depends on implementation
    // When one rectangle is completely contained, the algorithm may:
    // 1. Split the containing rectangle around the contained one
    // 2. Filter out invalid rectangles after splitting
    // 3. Keep both rectangles if they don't conflict
    // We verify the basic properties regardless of the exact behavior

    // Should have at least 1 rectangle (may have more if split)
    ASSERT_GE(result.size(), 1);

    // Verify all rectangles have even dimensions
    for (const auto &split_rect: result) {
        EXPECT_EQ(split_rect.bounds.width % 2, 0);
        EXPECT_EQ(split_rect.bounds.height % 2, 0);
        EXPECT_EQ(split_rect.bounds.left % 2, 0);
        EXPECT_EQ(split_rect.bounds.top % 2, 0);
    }

    // Verify rectangles don't overlap (basic check)
    for (size_t i = 0; i < result.size(); ++i) {
        for (size_t j = i + 1; j < result.size(); ++j) {
            // Rectangles from different objects shouldn't overlap
            if (result[i].original_object_id != result[j].original_object_id) {
                EXPECT_FALSE(result[i].bounds.intersects(result[j].bounds))
                        << "Rectangles from different objects should not overlap";
            }
        }
    }
}

/**
 * @brief Test splitting with rectangles at frame boundaries.
 */
TEST_F(RectangleSplitterTest, RectanglesAtFrameBoundaries) {
    std::vector<detected_object> tracked_objects;

    // Rectangle near top-left corner
    detected_object obj1(1);
    object_data data1(sf::rectangle{0, 0, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    // Rectangle near bottom-right corner
    detected_object obj2(2);
    object_data data2(sf::rectangle{1820, 980, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj2.history.push_back(data2);
    tracked_objects.push_back(obj2);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    ASSERT_EQ(result.size(), 2);

    // Verify rectangles stay within frame bounds
    for (const auto &split_rect: result) {
        EXPECT_GE(split_rect.bounds.left, 0);
        EXPECT_GE(split_rect.bounds.top, 0);
        EXPECT_LE(split_rect.bounds.left + split_rect.bounds.width, frame_width);
        EXPECT_LE(split_rect.bounds.top + split_rect.bounds.height, frame_height);
    }
}

/**
 * @brief Test splitting with edge-touching rectangles (should not split).
 */
TEST_F(RectangleSplitterTest, EdgeTouchingRectangles) {
    std::vector<detected_object> tracked_objects;

    // Two rectangles that touch but don't overlap
    detected_object obj1(1);
    object_data data1(sf::rectangle{100, 100, 50, 50}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    detected_object obj2(2);
    object_data data2(sf::rectangle{150, 100, 50, 50}, 0.8f, 1, sf::yuv_color::blue);
    obj2.history.push_back(data2);
    tracked_objects.push_back(obj2);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    // Should not split (no overlap)
    ASSERT_EQ(result.size(), 2);
    EXPECT_FALSE(result[0].was_split);
    EXPECT_FALSE(result[1].was_split);
}

/**
 * @brief Test split_rectangle display ID string methods.
 */
TEST_F(RectangleSplitterTest, SplitRectangleDisplayIds) {
    // Non-split rectangle
    split_rectangle non_split(sf::rectangle{100, 100, 50, 50}, 1, 0, sf::yuv_color::blue, 0.8f, false);
    EXPECT_EQ(non_split.get_display_id_string(), "1");
    EXPECT_EQ(non_split.get_combined_id_string(), "0-1");

    // Split rectangle
    split_rectangle split(sf::rectangle{100, 100, 50, 50}, 1, 2, sf::yuv_color::blue, 0.8f, true);
    EXPECT_EQ(split.get_display_id_string(), "2-1");
    EXPECT_EQ(split.get_combined_id_string(), "2-1");
}

/**
 * @brief Test extract_rectangles_from_split_rectangles conversion.
 */
TEST_F(RectangleSplitterTest, ExtractRectanglesFromSplitRectangles) {
    std::vector<split_rectangle> split_rects;
    split_rects.emplace_back(sf::rectangle{100, 100, 50, 50}, 1, 0, sf::yuv_color::blue, 0.8f, false);
    split_rects.emplace_back(sf::rectangle{200, 200, 50, 50}, 2, 0, sf::yuv_color::blue, 0.8f, false);

    const auto rectangles = extract_rectangles_from_split_rectangles(split_rects);

    ASSERT_EQ(rectangles.size(), 2);
    EXPECT_EQ(rectangles[0].left, 100);
    EXPECT_EQ(rectangles[0].top, 100);
    EXPECT_EQ(rectangles[1].left, 200);
    EXPECT_EQ(rectangles[1].top, 200);
}

/**
 * @brief Test splitting with odd dimensions (should be corrected to even).
 */
TEST_F(RectangleSplitterTest, OddDimensionsCorrection) {
    std::vector<detected_object> tracked_objects;

    // Rectangle with odd dimensions
    detected_object obj1(1);
    object_data data1(sf::rectangle{101, 101, 51, 51}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    ASSERT_EQ(result.size(), 1);

    // Verify even dimensions
    EXPECT_EQ(result[0].bounds.width % 2, 0);
    EXPECT_EQ(result[0].bounds.height % 2, 0);
    EXPECT_EQ(result[0].bounds.left % 2, 0);
    EXPECT_EQ(result[0].bounds.top % 2, 0);
}

/**
 * @brief Test splitting with multiple overlapping rectangles.
 */
TEST_F(RectangleSplitterTest, MultipleOverlappingRectangles) {
    std::vector<detected_object> tracked_objects;

    // Create three overlapping rectangles
    detected_object obj1(1);
    object_data data1(sf::rectangle{100, 100, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    detected_object obj2(2);
    object_data data2(sf::rectangle{150, 150, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj2.history.push_back(data2);
    tracked_objects.push_back(obj2);

    detected_object obj3(3);
    object_data data3(sf::rectangle{200, 200, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj3.history.push_back(data3);
    tracked_objects.push_back(obj3);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    // Should result in multiple split rectangles
    ASSERT_GE(result.size(), 3);

    // Verify all rectangles have even dimensions
    for (const auto &split_rect: result) {
        EXPECT_EQ(split_rect.bounds.width % 2, 0);
        EXPECT_EQ(split_rect.bounds.height % 2, 0);
        EXPECT_EQ(split_rect.bounds.left % 2, 0);
        EXPECT_EQ(split_rect.bounds.top % 2, 0);
    }
}

/**
 * @brief Test splitting with empty tracked objects list.
 */
TEST_F(RectangleSplitterTest, EmptyTrackedObjects) {
    std::vector<detected_object> tracked_objects;

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    EXPECT_TRUE(result.empty());
}

/**
 * @brief Test splitting with rectangles outside frame bounds (should be clamped).
 */
TEST_F(RectangleSplitterTest, RectanglesOutsideFrameBounds) {
    std::vector<detected_object> tracked_objects;

    // Rectangle that extends beyond frame boundaries
    detected_object obj1(1);
    object_data data1(sf::rectangle{1900, 1000, 100, 100}, 0.8f, 1, sf::yuv_color::blue);
    obj1.history.push_back(data1);
    tracked_objects.push_back(obj1);

    const auto result = split_overlapping_rectangles(tracked_objects, frame_width, frame_height, 1);

    ASSERT_EQ(result.size(), 1);

    // Verify rectangle is clamped to frame bounds
    EXPECT_LE(result[0].bounds.left + result[0].bounds.width, frame_width);
    EXPECT_LE(result[0].bounds.top + result[0].bounds.height, frame_height);
}

TEST_F(RectangleSplitterTest, SplitRectangleByOverlapReturnsEvenAdjustedOriginalWhenNoOverlap) {
    const sf::rectangle rect_to_split(101, 103, 51, 49);
    const sf::rectangle blocking_rect(500, 500, 10, 10);

    const auto result = split_rectangle_by_overlap(rect_to_split, blocking_rect, frame_width, frame_height);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0].left % 2, 0);
    EXPECT_EQ(result[0].top % 2, 0);
    EXPECT_EQ(result[0].width % 2, 0);
    EXPECT_EQ(result[0].height % 2, 0);
}

TEST_F(RectangleSplitterTest, SplitRectangleByOverlapProducesFourFragmentsForCenteredBlocker) {
    const sf::rectangle rect_to_split(0, 0, 10, 10);
    const sf::rectangle blocking_rect(2, 2, 6, 6);

    const auto result = split_rectangle_by_overlap(rect_to_split, blocking_rect, frame_width, frame_height);
    ASSERT_EQ(result.size(), 4U);
    for (const auto &fragment: result) {
        EXPECT_FALSE(fragment.intersects(blocking_rect));
        EXPECT_TRUE(fragment.is_valid());
    }
}

TEST_F(RectangleSplitterTest, SplitRectangleByOverlapReturnsEmptyWhenBlockingFullyContainsRectangle) {
    const sf::rectangle rect_to_split(10, 10, 20, 20);
    const sf::rectangle blocking_rect(0, 0, 100, 100);

    const auto result = split_rectangle_by_overlap(rect_to_split, blocking_rect, frame_width, frame_height);
    EXPECT_TRUE(result.empty());
}

TEST_F(RectangleSplitterTest, SplitRectangleByOverlapCreatesThreeFragmentsForSideOverlap) {
    const sf::rectangle rect_to_split(0, 0, 10, 10);
    const sf::rectangle blocking_rect(0, 2, 4, 4);

    const auto result = split_rectangle_by_overlap(rect_to_split, blocking_rect, frame_width, frame_height);
    ASSERT_EQ(result.size(), 3U);
    for (const auto &fragment: result) {
        EXPECT_TRUE(fragment.is_valid());
        EXPECT_FALSE(fragment.intersects(blocking_rect));
    }
}

TEST_F(RectangleSplitterTest, SplitRectangleByOverlapTouchingEdgesIsNotTreatedAsOverlap) {
    const sf::rectangle rect_to_split(0, 0, 10, 10);
    const sf::rectangle blocking_rect(10, 0, 5, 10); // touches right edge only

    const auto result = split_rectangle_by_overlap(rect_to_split, blocking_rect, frame_width, frame_height);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0].left, 0);
    EXPECT_EQ(result[0].top, 0);
    EXPECT_EQ(result[0].width, 10);
    EXPECT_EQ(result[0].height, 10);
}

TEST_F(RectangleSplitterTest, SplitRectangleByOverlapMaintainsEvenFragmentsNearFrameBounds) {
    const sf::rectangle rect_to_split(1917, 1077, 3, 3);
    const sf::rectangle blocking_rect(1918, 1078, 2, 2);

    const auto result = split_rectangle_by_overlap(rect_to_split, blocking_rect, frame_width, frame_height);
    ASSERT_FALSE(result.empty());
    for (const auto &fragment: result) {
        EXPECT_EQ(fragment.left % 2, 0);
        EXPECT_EQ(fragment.top % 2, 0);
        EXPECT_EQ(fragment.width % 2, 0);
        EXPECT_EQ(fragment.height % 2, 0);
        EXPECT_GE(fragment.left, 0);
        EXPECT_GE(fragment.top, 0);
        EXPECT_LE(fragment.left + fragment.width, frame_width);
        EXPECT_LE(fragment.top + fragment.height, frame_height);
    }
}
