// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/processing/roi_detection_processing/helpers.h"
#include "../../encoder_cli/src/roi/rectangle_splitter.h"
#include "../common/gst_test_fixture.h"

class RoiDetectionProcessingHelpersTest : public GStreamerTestFixture {};

TEST_F(RoiDetectionProcessingHelpersTest, InflateAndClampRoiClampsToTopLeftBoundary) {
    const sf::rectangle roi(5, 5, 10, 10);
    const sf::rectangle expanded = inflate_and_clamp_roi(roi, 20, 100, 80);

    EXPECT_EQ(expanded, sf::rectangle(0, 0, 35, 35));
}

TEST_F(RoiDetectionProcessingHelpersTest, InflateAndClampRoiClampsToBottomRightBoundary) {
    const sf::rectangle roi(90, 70, 20, 20);
    const sf::rectangle expanded = inflate_and_clamp_roi(roi, 20, 100, 80);

    EXPECT_EQ(expanded, sf::rectangle(70, 50, 30, 30));
}

TEST_F(RoiDetectionProcessingHelpersTest, InflateAndClampRoiReturnsEmptyForInvalidInputs) {
    EXPECT_EQ(inflate_and_clamp_roi(sf::rectangle(10, 10, 0, 20), 10, 100, 80), sf::rectangle(0, 0, 0, 0));
    EXPECT_EQ(inflate_and_clamp_roi(sf::rectangle(10, 10, 20, 20), 10, 0, 80), sf::rectangle(0, 0, 0, 0));
}

TEST_F(RoiDetectionProcessingHelpersTest, ApplyFixedRoiMarginPreservesOrderAndCount) {
    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(10, 10, 10, 10),
            sf::rectangle(90, 70, 20, 20),
    };

    const auto expanded = apply_fixed_roi_margin(input_rectangles, 10, 100, 80);

    ASSERT_EQ(expanded.size(), input_rectangles.size());
    EXPECT_EQ(expanded[0], sf::rectangle(0, 0, 30, 30));
    EXPECT_EQ(expanded[1], sf::rectangle(80, 60, 20, 20));
}

TEST_F(RoiDetectionProcessingHelpersTest, ApplyFixedRoiMarginWithIdsPreservesIdsAndOrder) {
    const std::vector<std::pair<sf::rectangle, std::string>> input_rectangles_with_ids = {
            {sf::rectangle(10, 10, 10, 10), "id_1"},
            {sf::rectangle(90, 70, 20, 20), "id_2"},
    };

    const auto expanded = apply_fixed_roi_margin_with_ids(input_rectangles_with_ids, 10, 100, 80);

    ASSERT_EQ(expanded.size(), input_rectangles_with_ids.size());
    EXPECT_EQ(expanded[0].first, sf::rectangle(0, 0, 30, 30));
    EXPECT_EQ(expanded[0].second, "id_1");
    EXPECT_EQ(expanded[1].first, sf::rectangle(80, 60, 20, 20));
    EXPECT_EQ(expanded[1].second, "id_2");
}

TEST_F(RoiDetectionProcessingHelpersTest, PrepareRectanglesWithIdsStaticModeUsesIndexIds) {
    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(1, 2, 10, 10),
            sf::rectangle(3, 4, 11, 12),
    };
    const std::vector<split_rectangle> split_rectangles;

    const auto result = prepare_rectangles_with_ids_for_random_access(input_rectangles, split_rectangles, true);

    ASSERT_EQ(result.size(), input_rectangles.size());
    EXPECT_EQ(result[0].second, "0");
    EXPECT_EQ(result[1].second, "1");
}

TEST_F(RoiDetectionProcessingHelpersTest, PrepareRectanglesWithIdsInferenceUsesObjectIdsWhenSizesMatch) {
    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(10, 10, 20, 20),
            sf::rectangle(40, 40, 30, 30),
    };
    const std::vector<split_rectangle> split_rectangles = {
            split_rectangle(input_rectangles[0], 101U, 0U, sf::yuv_color::blue, 0.9F, false),
            split_rectangle(input_rectangles[1], 202U, 0U, sf::yuv_color::blue, 0.8F, false),
    };

    const auto result = prepare_rectangles_with_ids_for_random_access(input_rectangles, split_rectangles, false);

    ASSERT_EQ(result.size(), input_rectangles.size());
    EXPECT_EQ(result[0].second, "101");
    EXPECT_EQ(result[1].second, "202");
}

TEST_F(RoiDetectionProcessingHelpersTest, PrepareRectanglesWithIdsFallbackCreatesUnknownIdsWhenNoMatch) {
    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(10, 10, 20, 20),
            sf::rectangle(40, 40, 30, 30),
    };
    const std::vector<split_rectangle> split_rectangles = {
            split_rectangle(input_rectangles[0], 303U, 0U, sf::yuv_color::blue, 0.9F, false),
    };

    const auto result = prepare_rectangles_with_ids_for_random_access(input_rectangles, split_rectangles, false);

    ASSERT_EQ(result.size(), input_rectangles.size());
    EXPECT_EQ(result[0].second, "303");
    EXPECT_EQ(result[1].second, "unknown_1");
}
