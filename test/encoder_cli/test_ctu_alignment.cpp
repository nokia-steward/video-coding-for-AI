// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/roi/low_delay_packer/ctu_alignment.h"
#include "../common/test_debug_categories.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_ctu_alignment.cpp
 * @brief Unit tests for CTU alignment utility functions.
 */

class CtuAlignmentTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
        init_all_test_debug_categories();
    }
};

// ---- align_down tests ----

TEST_F(CtuAlignmentTest, AlignDownExact) {
    EXPECT_EQ(align_down(64, 64), 64);
    EXPECT_EQ(align_down(128, 64), 128);
    EXPECT_EQ(align_down(0, 64), 0);
}

TEST_F(CtuAlignmentTest, AlignDownNonExact) {
    EXPECT_EQ(align_down(65, 64), 64);
    EXPECT_EQ(align_down(63, 64), 0);
    EXPECT_EQ(align_down(127, 64), 64);
    EXPECT_EQ(align_down(1, 64), 0);
}

TEST_F(CtuAlignmentTest, AlignDownLargeValues) {
    EXPECT_EQ(align_down(1920, 64), 1920); // 1920 = 30 * 64
    EXPECT_EQ(align_down(1080, 64), 1024); // 1024 = 16 * 64
    EXPECT_EQ(align_down(1921, 64), 1920);
}

// ---- align_up tests ----

TEST_F(CtuAlignmentTest, AlignUpExact) {
    EXPECT_EQ(align_up(64, 64), 64);
    EXPECT_EQ(align_up(128, 64), 128);
    EXPECT_EQ(align_up(0, 64), 0);
}

TEST_F(CtuAlignmentTest, AlignUpNonExact) {
    EXPECT_EQ(align_up(1, 64), 64);
    EXPECT_EQ(align_up(63, 64), 64);
    EXPECT_EQ(align_up(65, 64), 128);
    EXPECT_EQ(align_up(127, 64), 128);
}

TEST_F(CtuAlignmentTest, AlignUpLargeValues) {
    EXPECT_EQ(align_up(1920, 64), 1920);
    EXPECT_EQ(align_up(1080, 64), 1088); // 17 * 64 = 1088
    EXPECT_EQ(align_up(1921, 64), 1984); // 31 * 64 = 1984
}

// ---- compute_ctu_rectangle tests ----

TEST_F(CtuAlignmentTest, ComputeCtuRectangleAlignedRoi) {
    const sf::rectangle roi(0, 0, 64, 64);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.left, 0);
    EXPECT_EQ(ctu.top, 0);
    EXPECT_EQ(ctu.width, 64);
    EXPECT_EQ(ctu.height, 64);
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangleUnalignedRoi) {
    const sf::rectangle roi(10, 10, 50, 50);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.left, 0);
    EXPECT_EQ(ctu.top, 0);
    EXPECT_EQ(ctu.width, 64);
    EXPECT_EQ(ctu.height, 64);
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangleSpansTwoCtuColumns) {
    const sf::rectangle roi(50, 0, 30, 40);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.left, 0);
    EXPECT_EQ(ctu.top, 0);
    EXPECT_EQ(ctu.width, 128); // spans [0, 80) -> 2 CTUs
    EXPECT_EQ(ctu.height, 64);
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangleSpansTwoCtuRows) {
    const sf::rectangle roi(0, 50, 40, 30);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.left, 0);
    EXPECT_EQ(ctu.top, 0);
    EXPECT_EQ(ctu.width, 64);
    EXPECT_EQ(ctu.height, 128); // spans [0, 80) -> 2 CTUs
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangleLargeRoi) {
    const sf::rectangle roi(100, 200, 500, 400);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.left, 64); // align_down(100, 64)
    EXPECT_EQ(ctu.top, 192); // align_down(200, 64) = 192
    EXPECT_EQ(ctu.width, 576); // align_up(600, 64) - 64 = 640 - 64 = 576
    EXPECT_EQ(ctu.height, 448); // align_up(600, 64) - 192 = 640 - 192 = 448
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangleEmptyRoi) {
    const sf::rectangle roi(100, 200, 0, 50);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.width, 0);
    EXPECT_EQ(ctu.height, 0);
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangleZeroHeight) {
    const sf::rectangle roi(100, 200, 50, 0);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.width, 0);
    EXPECT_EQ(ctu.height, 0);
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangleAtOrigin) {
    const sf::rectangle roi(0, 0, 10, 10);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.left, 0);
    EXPECT_EQ(ctu.top, 0);
    EXPECT_EQ(ctu.width, 64);
    EXPECT_EQ(ctu.height, 64);
}

TEST_F(CtuAlignmentTest, ComputeCtuRectangle2x2Grid) {
    // ROI spans exactly a 2x2 CTU region
    const sf::rectangle roi(0, 0, 128, 128);
    const auto ctu = compute_ctu_rectangle(roi, 64);
    EXPECT_EQ(ctu.left, 0);
    EXPECT_EQ(ctu.top, 0);
    EXPECT_EQ(ctu.width, 128);
    EXPECT_EQ(ctu.height, 128);
}

// ---- center_roi_in_ctu_rect tests ----

TEST_F(CtuAlignmentTest, CenterRoiSmallerThanCtu) {
    const sf::rectangle roi(0, 0, 40, 30);
    const sf::rectangle ctu_rect(0, 0, 64, 64);
    const auto centered = center_roi_in_ctu_rect(roi, ctu_rect);
    EXPECT_EQ(centered.left, 12); // (64 - 40) / 2 = 12
    EXPECT_EQ(centered.top, 17); // (64 - 30) / 2 = 17
    EXPECT_EQ(centered.width, 40);
    EXPECT_EQ(centered.height, 30);
}

TEST_F(CtuAlignmentTest, CenterRoiExactlyFittingCtu) {
    const sf::rectangle roi(0, 0, 64, 64);
    const sf::rectangle ctu_rect(0, 0, 64, 64);
    const auto centered = center_roi_in_ctu_rect(roi, ctu_rect);
    EXPECT_EQ(centered.left, 0);
    EXPECT_EQ(centered.top, 0);
    EXPECT_EQ(centered.width, 64);
    EXPECT_EQ(centered.height, 64);
}

TEST_F(CtuAlignmentTest, CenterRoiInLargerCtuRect) {
    const sf::rectangle roi(0, 0, 80, 60);
    const sf::rectangle ctu_rect(128, 64, 128, 128);
    const auto centered = center_roi_in_ctu_rect(roi, ctu_rect);
    EXPECT_EQ(centered.left, 128 + (128 - 80) / 2); // 128 + 24 = 152
    EXPECT_EQ(centered.top, 64 + (128 - 60) / 2); // 64 + 34 = 98
    EXPECT_EQ(centered.width, 80);
    EXPECT_EQ(centered.height, 60);
}

// ---- compute_ctu_grid_dimensions tests ----

TEST_F(CtuAlignmentTest, CtuGridDimensionsSingleCell) {
    const sf::rectangle ctu_rect(0, 0, 64, 64);
    const auto [cols, rows] = compute_ctu_grid_dimensions(ctu_rect, 64);
    EXPECT_EQ(cols, 1);
    EXPECT_EQ(rows, 1);
}

TEST_F(CtuAlignmentTest, CtuGridDimensions2x3) {
    const sf::rectangle ctu_rect(0, 0, 128, 192);
    const auto [cols, rows] = compute_ctu_grid_dimensions(ctu_rect, 64);
    EXPECT_EQ(cols, 2);
    EXPECT_EQ(rows, 3);
}

// ---- ctu_rectangles_overlap tests ----

TEST_F(CtuAlignmentTest, CtuRectanglesOverlapTrue) {
    const sf::rectangle first_rect(0, 0, 128, 128);
    const sf::rectangle second_rect(64, 64, 128, 128);
    EXPECT_TRUE(ctu_rectangles_overlap(first_rect, second_rect));
}

TEST_F(CtuAlignmentTest, CtuRectanglesOverlapFalseAdjacent) {
    const sf::rectangle first_rect(0, 0, 64, 64);
    const sf::rectangle second_rect(64, 0, 64, 64);
    EXPECT_FALSE(ctu_rectangles_overlap(first_rect, second_rect));
}

TEST_F(CtuAlignmentTest, CtuRectanglesOverlapFalseSeparated) {
    const sf::rectangle first_rect(0, 0, 64, 64);
    const sf::rectangle second_rect(128, 128, 64, 64);
    EXPECT_FALSE(ctu_rectangles_overlap(first_rect, second_rect));
}

TEST_F(CtuAlignmentTest, CtuRectanglesOverlapSamePosition) {
    const sf::rectangle first_rect(0, 0, 64, 64);
    const sf::rectangle second_rect(0, 0, 64, 64);
    EXPECT_TRUE(ctu_rectangles_overlap(first_rect, second_rect));
}

// ---- find_ctu_grid_position tests ----

TEST_F(CtuAlignmentTest, FindCtuGridPositionReturnsFirstFreeCellRowMajor) {
    const std::vector<sf::rectangle> occupied = {sf::rectangle(0, 0, 64, 64)};
    const auto position = find_ctu_grid_position(64, 64, 128, 128, 64, occupied);

    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->left, 64);
    EXPECT_EQ(position->top, 0);
    EXPECT_EQ(position->width, 64);
    EXPECT_EQ(position->height, 64);
}

TEST_F(CtuAlignmentTest, FindCtuGridPositionSkipsConflictsForLargerCtuRects) {
    const std::vector<sf::rectangle> occupied = {
            sf::rectangle(0, 0, 128, 64),
            sf::rectangle(0, 64, 64, 64),
    };

    const auto position = find_ctu_grid_position(128, 64, 256, 128, 64, occupied);

    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->left, 128);
    EXPECT_EQ(position->top, 0);
    EXPECT_EQ(position->width, 128);
    EXPECT_EQ(position->height, 64);
}

TEST_F(CtuAlignmentTest, FindCtuGridPositionReturnsNulloptWhenGridIsFull) {
    const std::vector<sf::rectangle> occupied = {
            sf::rectangle(0, 0, 64, 64),
            sf::rectangle(64, 0, 64, 64),
            sf::rectangle(0, 64, 64, 64),
            sf::rectangle(64, 64, 64, 64),
    };

    const auto position = find_ctu_grid_position(64, 64, 128, 128, 64, occupied);
    EXPECT_FALSE(position.has_value());
}

TEST_F(CtuAlignmentTest, FindCtuGridPositionAllowsExactBoundaryFit) {
    const std::vector<sf::rectangle> occupied;
    const auto position = find_ctu_grid_position(128, 64, 128, 64, 64, occupied);

    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->left, 0);
    EXPECT_EQ(position->top, 0);
    EXPECT_EQ(position->width, 128);
    EXPECT_EQ(position->height, 64);
}
