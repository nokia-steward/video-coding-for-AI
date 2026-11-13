// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/roi/low_delay_packer.h"
#include "../../encoder_cli/src/roi/low_delay_packer/merging.h"
#include "../common/test_debug_categories.h"

class LowDelayMergingThresholdsTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
        init_all_test_debug_categories();
    }
};

TEST_F(LowDelayMergingThresholdsTest, CalculateOverlapPercentageAroundThreshold) {
    const sf::rectangle roi_a(0, 0, 100, 100);
    const sf::rectangle roi_b_below(71, 0, 100, 100); // 29% overlap
    const sf::rectangle roi_b_at(70, 0, 100, 100); // 30% overlap

    const double overlap_below = calculate_overlap_percentage(roi_a, roi_b_below);
    const double overlap_at = calculate_overlap_percentage(roi_a, roi_b_at);

    EXPECT_NEAR(overlap_below, 0.29, 1e-9);
    EXPECT_NEAR(overlap_at, 0.30, 1e-9);
    EXPECT_LT(overlap_below, 0.3);
    EXPECT_GE(overlap_at, 0.3);
}

TEST_F(LowDelayMergingThresholdsTest, MergeOverlappingRoisRecursiveRespectsThreshold) {
    const std::vector<std::pair<sf::rectangle, std::string>> below_threshold = {
            {sf::rectangle(0, 0, 100, 100), "obj_a"},
            {sf::rectangle(71, 0, 100, 100), "obj_b"},
    };
    const auto merged_below = merge_overlapping_rois_recursive(below_threshold, 0.3);
    ASSERT_EQ(merged_below.size(), 2U);

    const std::vector<std::pair<sf::rectangle, std::string>> at_threshold = {
            {sf::rectangle(0, 0, 100, 100), "obj_a"},
            {sf::rectangle(70, 0, 100, 100), "obj_b"},
    };
    const auto merged_at = merge_overlapping_rois_recursive(at_threshold, 0.3);
    ASSERT_EQ(merged_at.size(), 1U);
    EXPECT_EQ(merged_at[0].second.size(), 2U);
}

TEST_F(LowDelayMergingThresholdsTest, MergeOverlappingRoisRecursiveChainMergesConnectedComponents) {
    // A-B overlap at 30%, B-C overlap at 30%, A-C do not overlap.
    const std::vector<std::pair<sf::rectangle, std::string>> chain_input = {
            {sf::rectangle(0, 0, 100, 100), "obj_a"},
            {sf::rectangle(70, 0, 100, 100), "obj_b"},
            {sf::rectangle(140, 0, 100, 100), "obj_c"},
    };

    const auto merged = merge_overlapping_rois_recursive(chain_input, 0.3);
    ASSERT_EQ(merged.size(), 1U);
    EXPECT_EQ(merged[0].second.size(), 3U);
}

TEST_F(LowDelayMergingThresholdsTest, LowDelayMappingCardinalityChangesAtThreshold) {
    low_delay_packer packer(640, 480, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> below_threshold = {
            {sf::rectangle(0, 0, 100, 100), "obj_a"},
            {sf::rectangle(71, 0, 100, 100), "obj_b"},
    };
    const auto no_merge_result = packer.get_packed_rectangles_with_ids(below_threshold, 0);
    ASSERT_TRUE(no_merge_result.has_value());
    ASSERT_EQ(no_merge_result->original_to_packed_mapping.size(), 2U);
    ASSERT_EQ(no_merge_result->packed_rectangles.size(), 2U);
    EXPECT_NE(no_merge_result->original_to_packed_mapping[0], no_merge_result->original_to_packed_mapping[1]);

    const std::vector<std::pair<sf::rectangle, std::string>> at_threshold = {
            {sf::rectangle(0, 0, 100, 100), "obj_a"},
            {sf::rectangle(70, 0, 100, 100), "obj_b"},
    };
    const auto merge_result = packer.get_packed_rectangles_with_ids(at_threshold, 1);
    ASSERT_TRUE(merge_result.has_value());
    ASSERT_EQ(merge_result->original_to_packed_mapping.size(), 2U);
    ASSERT_EQ(merge_result->packed_rectangles.size(), 1U);
    EXPECT_EQ(merge_result->original_to_packed_mapping[0], merge_result->original_to_packed_mapping[1]);
}
