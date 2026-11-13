// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/detection_modes/inference/inference_detection.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/detection_modes/passthrough/passthrough_detection.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/detection_modes/static/static_detection.h"
#include "../../encoder_cli/src/roi/static_roi_data.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_detection_modes.cpp
 * @brief Tests for individual detection mode implementations.
 *
 * Tests verify that:
 * - Passthrough detection returns empty ROIs
 * - Static detection returns ROIs from loaded data
 * - Edge cases are handled correctly
 */

class DetectionModesTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }

    // Helper to create a dummy buffer
    sf::unique_gst_buffer_no_delete_ptr create_dummy_buffer() {
        const auto buffer = gst_buffer_new_allocate(nullptr, 100, nullptr);
        return sf::unique_gst_buffer_no_delete_ptr(buffer);
    }

    // Helper to create frame properties
    frame_properties create_frame_properties() {
        frame_properties props;
        props.width = 640;
        props.height = 480;
        props.format = "I420";
        return props;
    }

    sf::unique_gst_sample_ptr create_sample_with_frame_path(const frame_path_type path) {
        const auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 100, nullptr));
        auto sample = sf::unique_gst_sample_ptr(gst_sample_new(buffer.get(), nullptr, nullptr, nullptr));
        if (!sample) {
            return sample;
        }
        const bool attached = attach_frame_path_metadata(sample, path, 1);
        if (!attached) {
            return {};
        }
        return sample;
    }
};

/**
 * @brief Test that passthrough detection returns empty ROIs.
 */
TEST_F(DetectionModesTest, PassthroughReturnsEmptyRois) {
    encoder_config config;
    global_data data(std::move(config));

    const auto input_buffer = create_dummy_buffer();
    const auto props = create_frame_properties();
    const int frame_number = 0;

    const auto [input_rectangles, split_rectangles] = passthrough_detect_rois(data, input_buffer, props, frame_number);

    EXPECT_TRUE(input_rectangles.empty()) << "Passthrough should return empty input rectangles";
    EXPECT_TRUE(split_rectangles.empty()) << "Passthrough should return empty split rectangles";
}

/**
 * @brief Test that passthrough get_tracked_objects returns empty vector.
 */
TEST_F(DetectionModesTest, PassthroughReturnsEmptyTrackedObjects) {
    encoder_config config;
    global_data data(std::move(config));

    const auto tracked_objects = passthrough_get_tracked_objects(data);

    EXPECT_TRUE(tracked_objects.empty()) << "Passthrough should return empty tracked objects";
}

/**
 * @brief Test that passthrough works with different frame numbers.
 */
TEST_F(DetectionModesTest, PassthroughConsistentAcrossFrames) {
    encoder_config config;
    global_data data(std::move(config));

    const auto input_buffer = create_dummy_buffer();
    const auto props = create_frame_properties();

    // Test multiple frames
    for (int frame_number = 0; frame_number < 5; ++frame_number) {
        const auto [input_rectangles, split_rectangles] =
                passthrough_detect_rois(data, input_buffer, props, frame_number);

        EXPECT_TRUE(input_rectangles.empty()) << "Frame " << frame_number << " should return empty rectangles";
        EXPECT_TRUE(split_rectangles.empty()) << "Frame " << frame_number << " should return empty split rectangles";
    }
}

/**
 * @brief Test that static detection returns ROIs from loaded data.
 */
TEST_F(DetectionModesTest, StaticReturnsLoadedRois) {
    encoder_config config;
    global_data data(std::move(config));

    // Load static ROI data
    std::vector<static_roi_rect> static_rois = {static_roi_rect{10, 10, 40, 40}, static_roi_rect{100, 100, 50, 50}};
    data.loaded_static_roi_data = static_roi_data(static_rois);

    const auto input_buffer = create_dummy_buffer();
    const auto props = create_frame_properties();
    const int frame_number = 0;

    const auto [input_rectangles, split_rectangles] = static_detect_rois(data, input_buffer, props, frame_number);

    ASSERT_EQ(input_rectangles.size(), 2) << "Should return 2 ROIs from loaded data";
    EXPECT_EQ(input_rectangles[0].left, 10);
    EXPECT_EQ(input_rectangles[0].top, 10);
    EXPECT_EQ(input_rectangles[0].width, 40);
    EXPECT_EQ(input_rectangles[0].height, 40);

    EXPECT_EQ(input_rectangles[1].left, 100);
    EXPECT_EQ(input_rectangles[1].top, 100);
    EXPECT_EQ(input_rectangles[1].width, 50);
    EXPECT_EQ(input_rectangles[1].height, 50);

    EXPECT_TRUE(split_rectangles.empty()) << "Static mode should return empty split rectangles";
}

/**
 * @brief Test that static detection is frame-by-frame consistent.
 */
TEST_F(DetectionModesTest, StaticConsistentAcrossFrames) {
    encoder_config config;
    global_data data(std::move(config));

    // Load static ROI data
    std::vector<static_roi_rect> static_rois = {static_roi_rect{20, 20, 30, 30}};
    data.loaded_static_roi_data = static_roi_data(static_rois);

    const auto input_buffer = create_dummy_buffer();
    const auto props = create_frame_properties();

    // Test multiple frames - should return same ROIs
    std::vector<sf::rectangle> first_frame_rects;
    for (int frame_number = 0; frame_number < 5; ++frame_number) {
        const auto [input_rectangles, split_rectangles] = static_detect_rois(data, input_buffer, props, frame_number);

        if (frame_number == 0) {
            first_frame_rects = input_rectangles;
        } else {
            ASSERT_EQ(input_rectangles.size(), first_frame_rects.size())
                    << "Frame " << frame_number << " should have same number of ROIs";
            if (!input_rectangles.empty() && !first_frame_rects.empty()) {
                EXPECT_EQ(input_rectangles[0].left, first_frame_rects[0].left);
                EXPECT_EQ(input_rectangles[0].top, first_frame_rects[0].top);
                EXPECT_EQ(input_rectangles[0].width, first_frame_rects[0].width);
                EXPECT_EQ(input_rectangles[0].height, first_frame_rects[0].height);
            }
        }
    }
}

/**
 * @brief Test that static get_tracked_objects returns empty vector.
 */
TEST_F(DetectionModesTest, StaticReturnsEmptyTrackedObjects) {
    encoder_config config;
    global_data data(std::move(config));

    // Load static ROI data
    std::vector<static_roi_rect> static_rois = {static_roi_rect{10, 10, 40, 40}};
    data.loaded_static_roi_data = static_roi_data(static_rois);

    const auto tracked_objects = static_get_tracked_objects(data);

    EXPECT_TRUE(tracked_objects.empty()) << "Static mode should return empty tracked objects";
}

/**
 * @brief Test that static detection handles empty ROI data gracefully.
 */
TEST_F(DetectionModesTest, StaticHandlesEmptyData) {
    encoder_config config;
    global_data data(std::move(config));

    // Load empty static ROI data
    std::vector<static_roi_rect> empty_rois;
    data.loaded_static_roi_data = static_roi_data(empty_rois);

    const auto input_buffer = create_dummy_buffer();
    const auto props = create_frame_properties();
    const int frame_number = 0;

    // This should not crash - empty data should return empty rectangles
    const auto [input_rectangles, split_rectangles] = static_detect_rois(data, input_buffer, props, frame_number);

    EXPECT_TRUE(input_rectangles.empty()) << "Empty static data should return empty rectangles";
    EXPECT_TRUE(split_rectangles.empty()) << "Empty static data should return empty split rectangles";
}

/**
 * @brief Test that static detection works with many ROIs.
 */
TEST_F(DetectionModesTest, StaticHandlesManyRois) {
    encoder_config config;
    global_data data(std::move(config));

    // Load many static ROIs
    std::vector<static_roi_rect> many_rois;
    for (int i = 0; i < 100; ++i) {
        many_rois.push_back(static_roi_rect{i * 10, i * 10, 20, 20});
    }
    data.loaded_static_roi_data = static_roi_data(many_rois);

    const auto input_buffer = create_dummy_buffer();
    const auto props = create_frame_properties();
    const int frame_number = 0;

    const auto [input_rectangles, split_rectangles] = static_detect_rois(data, input_buffer, props, frame_number);

    EXPECT_EQ(input_rectangles.size(), 100) << "Should return all 100 ROIs";
    EXPECT_TRUE(split_rectangles.empty()) << "Should return empty split rectangles";
}

TEST_F(DetectionModesTest, InferenceTrackedObjectsInitiallyEmpty) {
    encoder_config config;
    global_data data(std::move(config));

    const auto tracked_objects = inference_get_tracked_objects(data);
    EXPECT_TRUE(tracked_objects.empty());
}

TEST_F(DetectionModesTest, InferenceBypassReturnsEmptyWithoutCachedRois) {
    encoder_config config;
    global_data data(std::move(config));
    const auto sample = create_sample_with_frame_path(frame_path_type::bypass);
    ASSERT_NE(sample.get(), nullptr);
    const sf::unique_gst_buffer_no_delete_ptr input_buffer(gst_sample_get_buffer(sample.get()));
    ASSERT_NE(input_buffer.get(), nullptr);

    const auto props = create_frame_properties();
    const auto [input_rectangles, split_rectangles] = inference_detect_rois(data, input_buffer, props, 1);
    EXPECT_TRUE(input_rectangles.empty());
    EXPECT_TRUE(split_rectangles.empty());
}

TEST_F(DetectionModesTest, InferenceBypassUsesCachedRois) {
    encoder_config config;
    global_data data(std::move(config));

    std::vector<split_rectangle> split_rectangles{
            split_rectangle(sf::rectangle(11, 22, 20, 20), 7U, 0U, sf::yuv_color::blue, 0.9F, false),
    };
    std::vector<sf::rectangle> input_rectangles{
            sf::rectangle(11, 22, 20, 20),
    };
    data.bypass.last_inference_roi.update(split_rectangles, input_rectangles, 10 * GST_MSECOND, 5U);

    const auto sample = create_sample_with_frame_path(frame_path_type::bypass);
    ASSERT_NE(sample.get(), nullptr);
    const sf::unique_gst_buffer_no_delete_ptr input_buffer(gst_sample_get_buffer(sample.get()));
    ASSERT_NE(input_buffer.get(), nullptr);

    const auto props = create_frame_properties();
    const auto [result_rectangles, result_split_rectangles] = inference_detect_rois(data, input_buffer, props, 6);

    ASSERT_EQ(result_rectangles.size(), 1U);
    ASSERT_EQ(result_split_rectangles.size(), 1U);
    EXPECT_EQ(result_rectangles[0].left, 11);
    EXPECT_EQ(result_rectangles[0].top, 22);
    EXPECT_EQ(result_rectangles[0].width, 20);
    EXPECT_EQ(result_rectangles[0].height, 20);
    EXPECT_EQ(result_split_rectangles[0].original_object_id, 7U);
}
