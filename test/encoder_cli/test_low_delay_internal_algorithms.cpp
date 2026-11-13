// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../encoder_cli/src/roi/low_delay_packer/conflicts.h"
#include "../../encoder_cli/src/roi/low_delay_packer/core.h"
#include "../../encoder_cli/src/roi/low_delay_packer/history.h"
#include "../../encoder_cli/src/roi/low_delay_packer/positioning.h"
#include "../common/gst_test_fixture.h"
#include "../libvcai/include/rectangle.h"

class LowDelayInternalAlgorithmsTest : public GStreamerTestFixture {};

TEST_F(LowDelayInternalAlgorithmsTest, FindConflictingRoisSortsByWeightAscending) {
    const sf::rectangle desired_rect(5, 5, 30, 30);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(0, 0, 40, 40), // area 1600
            sf::rectangle(10, 10, 10, 10), // area 100
            sf::rectangle(100, 100, 10, 10) // no overlap
    };
    const std::vector<size_t> occupied_indices = {11, 22, 33};

    const auto conflicts = find_conflicting_rois(desired_rect, occupied_positions, occupied_indices);
    ASSERT_EQ(conflicts.size(), 2U);
    EXPECT_EQ(conflicts[0], 22U);
    EXPECT_EQ(conflicts[1], 11U);
}

TEST_F(LowDelayInternalAlgorithmsTest, RelocateConflictingRoiPrefersRightPosition) {
    const sf::rectangle conflicting_roi(0, 0, 10, 10);
    const sf::rectangle grown_roi_rect(0, 0, 20, 20);
    const sf::rectangle grown_roi_previous_pos(5, 5, 20, 20);
    const std::vector<sf::rectangle> occupied_positions;

    const auto relocated = relocate_conflicting_roi(conflicting_roi, grown_roi_rect, grown_roi_previous_pos, 100, 100,
                                                    occupied_positions);
    ASSERT_TRUE(relocated.has_value());
    EXPECT_EQ(relocated->left, 25);
    EXPECT_EQ(relocated->top, 5);
    EXPECT_EQ(relocated->width, 10);
    EXPECT_EQ(relocated->height, 10);
}

TEST_F(LowDelayInternalAlgorithmsTest, RelocateConflictingRoiFallsBackToBelowWhenRightBlocked) {
    const sf::rectangle conflicting_roi(0, 0, 10, 10);
    const sf::rectangle grown_roi_rect(0, 0, 20, 20);
    const sf::rectangle grown_roi_previous_pos(5, 5, 20, 20);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(25, 5, 10, 10) // blocks right-side placement
    };

    const auto relocated = relocate_conflicting_roi(conflicting_roi, grown_roi_rect, grown_roi_previous_pos, 100, 100,
                                                    occupied_positions);
    ASSERT_TRUE(relocated.has_value());
    EXPECT_EQ(relocated->left, 5);
    EXPECT_EQ(relocated->top, 25);
}

TEST_F(LowDelayInternalAlgorithmsTest, ResolveConflictsRelocatesConflictingRoiAndKeepsDesiredFit) {
    const sf::rectangle grown_roi_rect(0, 0, 20, 20);
    const sf::rectangle grown_roi_previous_pos(0, 0, 20, 20);
    const sf::rectangle desired_position(0, 0, 20, 20);

    std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(5, 5, 10, 10),
    };
    std::vector<size_t> occupied_indices = {1};
    std::vector<sf::rectangle> packed_rects = {
            sf::rectangle(0, 0, 5, 5),
            sf::rectangle(5, 5, 10, 10),
    };

    const bool resolved = resolve_conflicts_for_grown_roi(grown_roi_rect, grown_roi_previous_pos, desired_position, 80,
                                                          80, occupied_positions, occupied_indices, packed_rects, 1);
    EXPECT_TRUE(resolved);
    EXPECT_FALSE(desired_position.intersects(packed_rects[1]));
    EXPECT_EQ(occupied_positions[0], packed_rects[1]);
}

TEST_F(LowDelayInternalAlgorithmsTest, ResolveConflictsReturnsFalseWhenNoValidPlacementExists) {
    const sf::rectangle grown_roi_rect(0, 0, 30, 30);
    const sf::rectangle grown_roi_previous_pos(0, 0, 30, 30);
    const sf::rectangle desired_position(0, 0, 30, 30);

    std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(5, 5, 10, 10),
    };
    std::vector<size_t> occupied_indices = {0};
    std::vector<sf::rectangle> packed_rects = {
            sf::rectangle(5, 5, 10, 10),
    };

    const bool resolved = resolve_conflicts_for_grown_roi(grown_roi_rect, grown_roi_previous_pos, desired_position, 30,
                                                          30, occupied_positions, occupied_indices, packed_rects, 2);
    EXPECT_FALSE(resolved);
}

TEST_F(LowDelayInternalAlgorithmsTest, FindPositionNearEdgesLowerRightReturnsBottomRightWhenUnoccupied) {
    const sf::rectangle rect(0, 0, 16, 16);
    const std::vector<sf::rectangle> occupied_positions;

    const auto position = find_position_near_edges_lower_right(rect, 64, 64, occupied_positions);
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->left, 48);
    EXPECT_EQ(position->top, 48);
}

TEST_F(LowDelayInternalAlgorithmsTest, FindPositionNearOriginFindsCloseTopLeftPlacement) {
    const sf::rectangle rect(0, 0, 16, 16);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(0, 0, 16, 16),
    };

    const auto position = find_position_near_origin(rect, 128, 128, 16, occupied_positions);
    ASSERT_TRUE(position.has_value());
    EXPECT_FALSE(position->left == 0 && position->top == 0);
    EXPECT_LE(position->left, 16);
    EXPECT_LE(position->top, 16);
}

TEST_F(LowDelayInternalAlgorithmsTest, FindBestPositionReturnsNulloptWhenFrameIsFull) {
    const sf::rectangle rect(0, 0, 16, 16);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(0, 0, 16, 16),
    };

    const auto position = find_best_position(rect, 16, 16, 16, occupied_positions);
    EXPECT_FALSE(position.has_value());
}

TEST_F(LowDelayInternalAlgorithmsTest, FindCompactPositionPrefersNearestAdjacencyCandidate) {
    const sf::rectangle rect(0, 0, 10, 10);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(0, 0, 10, 10),
            sf::rectangle(20, 0, 10, 10),
    };

    const auto position = find_compact_position(rect, 100, 100, occupied_positions);
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->left, 10);
    EXPECT_EQ(position->top, 0);
}

TEST_F(LowDelayInternalAlgorithmsTest, FindCompactPositionReturnsNulloptWhenRectangleCannotFit) {
    const sf::rectangle rect(0, 0, 32, 32);
    const std::vector<sf::rectangle> occupied_positions;

    const auto position = find_compact_position(rect, 16, 16, occupied_positions);
    EXPECT_FALSE(position.has_value());
}

TEST_F(LowDelayInternalAlgorithmsTest, FindCornerPositionFallsBackFromOccupiedTopLeft) {
    const sf::rectangle rect(0, 0, 10, 10);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(0, 0, 10, 10),
    };

    const auto position = find_corner_position(rect, 50, 50, occupied_positions);
    ASSERT_TRUE(position.has_value());
    // Sorted by Manhattan distance, so bottom-left is checked before top-right.
    EXPECT_EQ(position->left, 0);
    EXPECT_EQ(position->top, 40);
}

TEST_F(LowDelayInternalAlgorithmsTest, FindEdgePositionFallsBackFromBlockedLeftEdgeToTopEdge) {
    const sf::rectangle rect(0, 0, 10, 10);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(0, 0, 10, 50),
    };

    const auto position = find_edge_position(rect, 50, 50, occupied_positions);
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->left, 10);
    EXPECT_EQ(position->top, 0);
}

TEST_F(LowDelayInternalAlgorithmsTest, FindWeightedPositionReturnsNulloptWhenFrameHasNoSpace) {
    const sf::rectangle rect(0, 0, 16, 16);
    const std::vector<sf::rectangle> occupied_positions = {
            sf::rectangle(0, 0, 16, 16),
    };

    const auto position = find_weighted_position(rect, /*weight=*/20000, 16, 16, occupied_positions);
    EXPECT_FALSE(position.has_value());
}

TEST_F(LowDelayInternalAlgorithmsTest, UpdateRoiHistorySetsObjectIdAndTrimsToMaxFrames) {
    std::unordered_map<std::string, roi_history> histories;

    update_roi_history(histories, "obj-1", sf::rectangle(0, 0, 10, 10), 2);
    update_roi_history(histories, "obj-1", sf::rectangle(1, 2, 10, 10), 2);
    update_roi_history(histories, "obj-1", sf::rectangle(2, 4, 10, 10), 2);

    ASSERT_TRUE(histories.contains("obj-1"));
    const auto &history = histories.at("obj-1");
    EXPECT_EQ(history.object_id, "obj-1");
    ASSERT_EQ(history.frames.size(), 2U);
    EXPECT_EQ(history.frames.front(), sf::rectangle(1, 2, 10, 10));
    EXPECT_EQ(history.frames.back(), sf::rectangle(2, 4, 10, 10));
}

TEST_F(LowDelayInternalAlgorithmsTest, AccumulateRoisForObjectReturnsNulloptForMissingHistory) {
    std::unordered_map<std::string, roi_history> histories;
    EXPECT_FALSE(accumulate_rois_for_object(histories, "missing").has_value());
}

TEST_F(LowDelayInternalAlgorithmsTest, AccumulateRoisForObjectReturnsBoundingUnionAcrossFrames) {
    std::unordered_map<std::string, roi_history> histories;
    update_roi_history(histories, "obj-acc", sf::rectangle(10, 20, 30, 40), 5);
    update_roi_history(histories, "obj-acc", sf::rectangle(5, 25, 20, 10), 5);
    update_roi_history(histories, "obj-acc", sf::rectangle(25, 10, 15, 25), 5);

    const auto accumulated = accumulate_rois_for_object(histories, "obj-acc");
    ASSERT_TRUE(accumulated.has_value());
    EXPECT_EQ(*accumulated, sf::rectangle(5, 10, 35, 50));
}

TEST_F(LowDelayInternalAlgorithmsTest, PackRectanglesReturnsEmptyResultWithoutChangingCountersForEmptyInput) {
    std::vector<stable_rectangle> rectangles;
    int pack_call_counter = 7;
    std::unordered_map<std::string, position_slot> position_memory;
    std::unordered_map<std::string, slot_reservation> slot_reservations;
    int compaction_wait_counter = 3;
    size_t total_stable_count = 11;
    size_t total_repacked_count = 13;

    const auto result =
            pack_rectangles(rectangles, 42, 1920, 1080, 64, pack_call_counter, position_memory, slot_reservations,
                            compaction_wait_counter, total_stable_count, total_repacked_count);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->packed_rectangles.empty());
    EXPECT_EQ(result->result_size, sf::rectangle(0, 0, 0, 0));
    EXPECT_EQ(result->stable_count, 0U);
    EXPECT_EQ(result->repacked_count, 0U);
    EXPECT_EQ(pack_call_counter, 7);
}

TEST_F(LowDelayInternalAlgorithmsTest, PackRectanglesHandlesSingleRectangleAndIncrementsCallCounter) {
    std::vector<stable_rectangle> rectangles;
    rectangles.emplace_back(sf::rectangle(0, 0, 16, 16), "obj-1");

    int pack_call_counter = 0;
    std::unordered_map<std::string, position_slot> position_memory;
    std::unordered_map<std::string, slot_reservation> slot_reservations;
    int compaction_wait_counter = 0;
    size_t total_stable_count = 0;
    size_t total_repacked_count = 0;

    const auto result =
            pack_rectangles(rectangles, 1, 128, 128, 16, pack_call_counter, position_memory, slot_reservations,
                            compaction_wait_counter, total_stable_count, total_repacked_count);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->packed_rectangles.size(), 1U);
    EXPECT_EQ(pack_call_counter, 1);
}
