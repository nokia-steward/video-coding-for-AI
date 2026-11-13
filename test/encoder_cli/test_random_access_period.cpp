// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

#include "../../encoder_cli/src/roi/random_access_packer/history.h"
#include "../../encoder_cli/src/roi/random_access_packer/period.h"
#include "../common/gst_test_fixture.h"

class RandomAccessPeriodTest : public GStreamerTestFixture {};

TEST_F(RandomAccessPeriodTest, EmptyHistoriesReturnEmptyResultAndIncrementCounter) {
    std::unordered_map<std::string, object_roi_history> object_histories;
    std::unordered_map<std::string, position_slot> position_memory;
    int pack_call_counter = 0;

    const auto result = process_intra_period(object_histories, 320, 240, 64, pack_call_counter, position_memory);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->sorted_source_rectangles.empty());
    EXPECT_TRUE(result->packed_rectangles.empty());
    EXPECT_TRUE(result->original_to_packed_mapping.empty());
    EXPECT_EQ(result->result_size.width, 0);
    EXPECT_EQ(result->result_size.height, 0);
    EXPECT_EQ(pack_call_counter, 1);
}

TEST_F(RandomAccessPeriodTest, SortsInputByObjectIdAndMergesOverlappingAccumulatedRois) {
    std::unordered_map<std::string, object_roi_history> object_histories;
    object_histories.emplace("b", object_roi_history("b"));
    object_histories.emplace("a", object_roi_history("a"));

    object_histories.at("b").rois.push_back(sf::rectangle(0, 0, 40, 40));
    object_histories.at("a").rois.push_back(sf::rectangle(10, 10, 40, 40));

    std::unordered_map<std::string, position_slot> position_memory;
    int pack_call_counter = 0;

    const auto result = process_intra_period(object_histories, 512, 512, 64, pack_call_counter, position_memory);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sorted_source_rectangles.size(), 2U);

    // Object IDs are sorted lexicographically, so "a" is first.
    EXPECT_EQ(result->sorted_source_rectangles[0].left, 10);
    EXPECT_EQ(result->sorted_source_rectangles[0].top, 10);
    EXPECT_EQ(result->sorted_source_rectangles[1].left, 0);
    EXPECT_EQ(result->sorted_source_rectangles[1].top, 0);

    // The two accumulated ROIs overlap above threshold and should map to one packed ROI.
    EXPECT_EQ(result->packed_rectangles.size(), 1U);
    ASSERT_EQ(result->original_to_packed_mapping.size(), 2U);
    EXPECT_EQ(result->original_to_packed_mapping[0], result->original_to_packed_mapping[1]);
    EXPECT_EQ(pack_call_counter, 1);
}

TEST_F(RandomAccessPeriodTest, KeepsSeparatePackedRoisWhenAccumulatedRoisDoNotOverlap) {
    std::unordered_map<std::string, object_roi_history> object_histories;
    object_histories.emplace("b", object_roi_history("b"));
    object_histories.emplace("a", object_roi_history("a"));

    object_histories.at("a").rois.push_back(sf::rectangle(0, 0, 20, 20));
    object_histories.at("b").rois.push_back(sf::rectangle(200, 200, 20, 20));

    std::unordered_map<std::string, position_slot> position_memory;
    int pack_call_counter = 0;

    const auto result = process_intra_period(object_histories, 512, 512, 64, pack_call_counter, position_memory);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sorted_source_rectangles.size(), 2U);
    EXPECT_EQ(result->packed_rectangles.size(), 2U);
    ASSERT_EQ(result->original_to_packed_mapping.size(), 2U);
    EXPECT_NE(result->original_to_packed_mapping[0], result->original_to_packed_mapping[1]);
}
