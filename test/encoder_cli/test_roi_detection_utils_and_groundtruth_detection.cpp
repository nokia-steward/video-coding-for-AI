// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/detection_modes/groundtruth/groundtruth_detection.h"
#include "../../encoder_cli/src/processing/roi_detection_utils.h"
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

class RoiDetectionUtilsAndGroundtruthDetectionTest : public GStreamerTestFixture {};

TEST_F(RoiDetectionUtilsAndGroundtruthDetectionTest, ClipRectangleToFrameClampsCoordinatesAndDimensions) {
    const sf::rectangle rect(-5, -7, 40, 50);
    const auto clipped = clip_rectangle_to_frame(rect, 32, 24);
    EXPECT_EQ(clipped.left, 0);
    EXPECT_EQ(clipped.top, 0);
    EXPECT_EQ(clipped.width, 32);
    EXPECT_EQ(clipped.height, 24);

    const sf::rectangle out_of_bounds_start(100, 80, 10, 10);
    const auto clipped_oob = clip_rectangle_to_frame(out_of_bounds_start, 64, 48);
    EXPECT_EQ(clipped_oob.left, 63);
    EXPECT_EQ(clipped_oob.top, 47);
    EXPECT_EQ(clipped_oob.width, 1);
    EXPECT_EQ(clipped_oob.height, 1);
}

TEST_F(RoiDetectionUtilsAndGroundtruthDetectionTest, IsValidRectangleRequiresPositiveSizeAndInBoundsOrigin) {
    EXPECT_TRUE(is_valid_rectangle(sf::rectangle(0, 0, 10, 10), 100, 100));
    EXPECT_TRUE(is_valid_rectangle(sf::rectangle(99, 99, 1, 1), 100, 100));
    EXPECT_FALSE(is_valid_rectangle(sf::rectangle(-1, 0, 10, 10), 100, 100));
    EXPECT_FALSE(is_valid_rectangle(sf::rectangle(0, -1, 10, 10), 100, 100));
    EXPECT_FALSE(is_valid_rectangle(sf::rectangle(100, 0, 1, 1), 100, 100));
    EXPECT_FALSE(is_valid_rectangle(sf::rectangle(0, 100, 1, 1), 100, 100));
    EXPECT_FALSE(is_valid_rectangle(sf::rectangle(0, 0, 0, 10), 100, 100));
    EXPECT_FALSE(is_valid_rectangle(sf::rectangle(0, 0, 10, 0), 100, 100));
}

TEST_F(RoiDetectionUtilsAndGroundtruthDetectionTest, GroundtruthDetectionReturnsEmptyWhenNoDataLoaded) {
    auto config = make_base_config();
    global_data data(std::move(config));

    const frame_properties props{.width = 640, .height = 480, .format = "I420"};
    const sf::unique_gst_buffer_no_delete_ptr input_buffer(nullptr);

    const auto [input_rectangles, split_rectangles] = groundtruth_detect_rois(data, input_buffer, props, 7);
    EXPECT_TRUE(input_rectangles.empty());
    EXPECT_TRUE(split_rectangles.empty());
}

TEST_F(RoiDetectionUtilsAndGroundtruthDetectionTest, GroundtruthDetectionReturnsFrameRectanglesAndEmptySplits) {
    auto config = make_base_config();
    global_data data(std::move(config));

    std::unordered_map<int, std::vector<sf::rectangle>> frame_map;
    frame_map.emplace(3, std::vector<sf::rectangle>{sf::rectangle(11, 13, 17, 19), sf::rectangle(20, 30, 40, 50)});
    frame_map.emplace(4, std::vector<sf::rectangle>{sf::rectangle(1, 2, 3, 4)});
    data.loaded_groundtruth_roi_data = groundtruth_roi_data(std::move(frame_map));

    const frame_properties props{.width = 640, .height = 480, .format = "I420"};
    const sf::unique_gst_buffer_no_delete_ptr input_buffer(nullptr);

    const auto [frame_three_rectangles, frame_three_splits] = groundtruth_detect_rois(data, input_buffer, props, 3);
    ASSERT_EQ(frame_three_rectangles.size(), 2U);
    EXPECT_EQ(frame_three_rectangles[0], sf::rectangle(11, 13, 17, 19));
    EXPECT_EQ(frame_three_rectangles[1], sf::rectangle(20, 30, 40, 50));
    EXPECT_TRUE(frame_three_splits.empty());

    const auto [missing_rectangles, missing_splits] = groundtruth_detect_rois(data, input_buffer, props, 99);
    EXPECT_TRUE(missing_rectangles.empty());
    EXPECT_TRUE(missing_splits.empty());
}

TEST_F(RoiDetectionUtilsAndGroundtruthDetectionTest, GroundtruthGetTrackedObjectsAlwaysReturnsEmpty) {
    auto config = make_base_config();
    global_data data(std::move(config));

    const auto tracked_objects = groundtruth_get_tracked_objects(data);
    EXPECT_TRUE(tracked_objects.empty());
}
