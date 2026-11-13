// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../encoder_cli/src/roi/low_delay_packer.h"
#include "../../encoder_cli/src/roi/low_delay_packer/ctu_alignment.h"
#include "../common/test_debug_categories.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_low_delay_packer_ctu.cpp
 * @brief Tests for CTU-aligned low delay packer functionality.
 *
 * Verifies:
 * - First-occurrence centering within CTU rectangles.
 * - CTU non-overlap constraint.
 * - Pixel correspondence preservation across frames.
 * - Shrinking ROI handling (CTU rows/columns freed, other ROIs not moved).
 * - Growing ROI that fits in place.
 * - Growing ROI that must relocate (does not displace other ROIs).
 * - Multiple ROIs with CTU alignment.
 * - ROI disappearance/reappearance with slot reservation.
 * - Determinism.
 */

class LowDelayPackerCtuTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
        init_all_test_debug_categories();
    }
};

// ---- First-occurrence centering ----

TEST_F(LowDelayPackerCtuTest, FirstOccurrenceSingleRoiIsCentered) {
    low_delay_packer packer(1920, 1080, 64, 3);

    // Source ROI at (100,100) with size 50x40.
    // compute_ctu_rectangle({100,100,50,40}, 64) produces a 128x128 CTU rect
    // (x spans [64,192), y spans [64,192) -> 2x2 CTUs).
    // The CTU rect is placed at grid position (0,0) -> packed CTU rect = (0,0,128,128).
    // Centering: offset_x = (128-50)/2 = 39, offset_y = (128-40)/2 = 44.
    const std::vector<std::pair<sf::rectangle, std::string>> rois = {{{100, 100, 50, 40}, "obj1"}};

    const auto result = packer.get_packed_rectangles_with_ids(rois, 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->packed_rectangles.empty());

    const auto &packed = result->packed_rectangles[0];
    EXPECT_EQ(packed.width, 50);
    EXPECT_EQ(packed.height, 40);

    // The ROI's source-space CTU rectangle is 128x128 (2x2 CTUs).
    // Placed at (0,0), centering offsets are (128-50)/2=39 and (128-40)/2=44.
    EXPECT_EQ(packed.left, (128 - 50) / 2);
    EXPECT_EQ(packed.top, (128 - 40) / 2);
}

// ---- CTU non-overlap ----

TEST_F(LowDelayPackerCtuTest, TwoRoisPlacedInDifferentCtuCells) {
    low_delay_packer packer(1920, 1080, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> rois = {{{100, 100, 50, 40}, "obj1"},
                                                                     {{200, 200, 60, 50}, "obj2"}};

    const auto result = packer.get_packed_rectangles_with_ids(rois, 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->packed_rectangles.size(), 2);

    // Compute CTU rects for both packed positions
    const auto ctu1 = compute_ctu_rectangle(result->packed_rectangles[0], 64);
    const auto ctu2 = compute_ctu_rectangle(result->packed_rectangles[1], 64);

    // They must not overlap
    EXPECT_FALSE(ctu_rectangles_overlap(ctu1, ctu2));
}

// ---- Pixel correspondence ----

TEST_F(LowDelayPackerCtuTest, PixelCorrespondencePreservedOnSourceShift) {
    low_delay_packer packer(1920, 1080, 64, 1);

    // Frame 0: ROI at (200, 200, 100, 80)
    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {{{200, 200, 100, 80}, "obj1"}};
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    const auto packed0 = result0->packed_rectangles[0];

    // Frame 1: ROI shifted by (10, 5) -> (210, 205, 100, 80)
    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {{{210, 205, 100, 80}, "obj1"}};
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());
    const auto packed1 = result1->packed_rectangles[0];

    // The packed position should shift by (10, 5)
    EXPECT_EQ(packed1.left - packed0.left, 10);
    EXPECT_EQ(packed1.top - packed0.top, 5);
    EXPECT_EQ(packed1.width, 100);
    EXPECT_EQ(packed1.height, 80);
}

TEST_F(LowDelayPackerCtuTest, PixelCorrespondenceCumulativeShift) {
    low_delay_packer packer(1920, 1080, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {{{200, 200, 100, 80}, "obj1"}};
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    const auto packed0 = result0->packed_rectangles[0];

    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {{{210, 205, 100, 80}, "obj1"}};
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());

    // Frame 2: another (10, 5) shift -> (220, 210, 100, 80)
    const std::vector<std::pair<sf::rectangle, std::string>> frame2 = {{{220, 210, 100, 80}, "obj1"}};
    const auto result2 = packer.get_packed_rectangles_with_ids(frame2, 2);
    ASSERT_TRUE(result2.has_value());
    const auto packed2 = result2->packed_rectangles[0];

    // Cumulative shift from frame 0: (20, 10)
    EXPECT_EQ(packed2.left - packed0.left, 20);
    EXPECT_EQ(packed2.top - packed0.top, 10);
}

// ---- Shrinking ROI ----

TEST_F(LowDelayPackerCtuTest, ShrinkingRoiDoesNotMoveOtherRois) {
    low_delay_packer packer(1920, 1080, 64, 1);

    // Frame 0: two ROIs
    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {{{100, 100, 100, 80}, "obj1"},
                                                                       {{500, 500, 60, 40}, "obj2"}};
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    const auto obj2_packed0 = result0->packed_rectangles[1];

    // Frame 1: obj1 shrinks, obj2 stays the same
    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {{{100, 100, 50, 40}, "obj1"},
                                                                       {{500, 500, 60, 40}, "obj2"}};
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());
    const auto obj2_packed1 = result1->packed_rectangles[1];

    // obj2 position should remain unchanged
    EXPECT_EQ(obj2_packed1.left, obj2_packed0.left);
    EXPECT_EQ(obj2_packed1.top, obj2_packed0.top);
}

// ---- Growing ROI that fits in place ----

TEST_F(LowDelayPackerCtuTest, GrowingRoiThatFitsInPlace) {
    low_delay_packer packer(1920, 1080, 64, 1);

    // Frame 0: small ROI within one CTU
    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {{{200, 200, 30, 20}, "obj1"}};
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());

    // Frame 1: slightly larger but still within same CTU rectangle dimensions
    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {{{200, 200, 40, 30}, "obj1"}};
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->packed_rectangles[0].width, 40);
    EXPECT_EQ(result1->packed_rectangles[0].height, 30);
}

// ---- Multiple ROIs with CTU alignment ----

TEST_F(LowDelayPackerCtuTest, MultipleRoisAllCtuAligned) {
    low_delay_packer packer(1920, 1080, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> rois = {
            {{100, 100, 200, 150}, "obj1"}, {{400, 300, 100, 80}, "obj2"}, {{700, 100, 50, 50}, "obj3"}};

    const auto result = packer.get_packed_rectangles_with_ids(rois, 0);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->packed_rectangles.size(), 3);

    // Verify all packed rects have valid dimensions
    for (size_t i = 0; i < result->packed_rectangles.size(); ++i) {
        const auto &packed = result->packed_rectangles[i];
        EXPECT_GT(packed.width, 0) << "ROI " << i << " has zero width";
        EXPECT_GT(packed.height, 0) << "ROI " << i << " has zero height";
    }

    // Verify no CTU-level overlaps between any pair
    for (size_t i = 0; i < result->packed_rectangles.size(); ++i) {
        const auto ctu_i = compute_ctu_rectangle(result->packed_rectangles[i], 64);
        for (size_t j = i + 1; j < result->packed_rectangles.size(); ++j) {
            const auto ctu_j = compute_ctu_rectangle(result->packed_rectangles[j], 64);
            EXPECT_FALSE(ctu_rectangles_overlap(ctu_i, ctu_j)) << "CTU overlap between ROI " << i << " and " << j;
        }
    }
}

// ---- ROI disappearance and reappearance ----

TEST_F(LowDelayPackerCtuTest, DisappearedRoiSlotReservedAndReused) {
    low_delay_packer packer(1920, 1080, 64, 1);

    // Frame 0: two ROIs
    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {{{100, 100, 80, 60}, "obj1"},
                                                                       {{400, 200, 60, 50}, "obj2"}};
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    const auto obj2_packed0 = result0->packed_rectangles[1];

    // Frame 1: obj2 disappears
    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {{{100, 100, 80, 60}, "obj1"}};
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());

    // Frame 2: obj2 reappears at same source position
    const std::vector<std::pair<sf::rectangle, std::string>> frame2 = {{{100, 100, 80, 60}, "obj1"},
                                                                       {{400, 200, 60, 50}, "obj2"}};
    const auto result2 = packer.get_packed_rectangles_with_ids(frame2, 2);
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2->packed_rectangles.size(), 2);

    // obj2 should get a valid position
    EXPECT_GT(result2->packed_rectangles[1].width, 0);
    EXPECT_GT(result2->packed_rectangles[1].height, 0);
}

// ---- Determinism ----

TEST_F(LowDelayPackerCtuTest, DeterministicResults) {
    const std::vector<std::pair<sf::rectangle, std::string>> rois = {{{100, 100, 200, 150}, "obj1"},
                                                                     {{400, 300, 100, 80}, "obj2"}};

    low_delay_packer packer1(1920, 1080, 64, 1);
    const auto result1 = packer1.get_packed_rectangles_with_ids(rois, 0);

    low_delay_packer packer2(1920, 1080, 64, 1);
    const auto result2 = packer2.get_packed_rectangles_with_ids(rois, 0);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result1->packed_rectangles.size(), result2->packed_rectangles.size());

    for (size_t i = 0; i < result1->packed_rectangles.size(); ++i) {
        EXPECT_EQ(result1->packed_rectangles[i].left, result2->packed_rectangles[i].left) << "Mismatch at index " << i;
        EXPECT_EQ(result1->packed_rectangles[i].top, result2->packed_rectangles[i].top) << "Mismatch at index " << i;
        EXPECT_EQ(result1->packed_rectangles[i].width, result2->packed_rectangles[i].width)
                << "Mismatch at index " << i;
        EXPECT_EQ(result1->packed_rectangles[i].height, result2->packed_rectangles[i].height)
                << "Mismatch at index " << i;
    }
}

// ---- Result size is CTU-aligned ----

TEST_F(LowDelayPackerCtuTest, ResultSizeIsCtuAligned) {
    low_delay_packer packer(1920, 1080, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> rois = {{{100, 100, 200, 150}, "obj1"}};
    const auto result = packer.get_packed_rectangles_with_ids(rois, 0);
    ASSERT_TRUE(result.has_value());

    // result_size should be CTU-aligned (dimensions are multiples of 64)
    EXPECT_EQ(result->result_size.left % 64, 0);
    EXPECT_EQ(result->result_size.top % 64, 0);
    EXPECT_EQ(result->result_size.width % 64, 0);
    EXPECT_EQ(result->result_size.height % 64, 0);
}

// ---- Empty input ----

TEST_F(LowDelayPackerCtuTest, EmptyInputReturnsEmptyResult) {
    low_delay_packer packer(1920, 1080, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> rois = {};
    const auto result = packer.get_packed_rectangles_with_ids(rois, 0);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->packed_rectangles.empty());
}

// ---- Weight calculation still works ----

TEST_F(LowDelayPackerCtuTest, WeightCalculationUnchanged) {
    EXPECT_EQ(low_delay_packer::get_weight(sf::rectangle(0, 0, 100, 50)), 5000);
    EXPECT_EQ(low_delay_packer::get_weight(sf::rectangle(0, 0, 200, 100)), 20000);
}

// ---- Pixel correspondence regression test (multi-frame, two ROIs) ----

TEST_F(LowDelayPackerCtuTest, PixelCorrespondenceTwoRoisMultiFrame) {
    low_delay_packer packer(1920, 1080, 64, 1);

    // Frame 0
    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {{{200, 100, 100, 80}, "obj1"},
                                                                       {{600, 400, 120, 90}, "obj2"}};
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    ASSERT_EQ(result0->packed_rectangles.size(), 2);
    const auto packed0_obj1 = result0->packed_rectangles[0];
    const auto packed0_obj2 = result0->packed_rectangles[1];

    // Frame 1: both shift by (10, 5)
    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {{{210, 105, 100, 80}, "obj1"},
                                                                       {{610, 405, 120, 90}, "obj2"}};
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());
    const auto packed1_obj1 = result1->packed_rectangles[0];
    const auto packed1_obj2 = result1->packed_rectangles[1];

    EXPECT_EQ(packed1_obj1.left - packed0_obj1.left, 10);
    EXPECT_EQ(packed1_obj1.top - packed0_obj1.top, 5);
    EXPECT_EQ(packed1_obj2.left - packed0_obj2.left, 10);
    EXPECT_EQ(packed1_obj2.top - packed0_obj2.top, 5);

    // Frame 2: another shift (10, 5) -> cumulative (20, 10) from frame 0
    const std::vector<std::pair<sf::rectangle, std::string>> frame2 = {{{220, 110, 100, 80}, "obj1"},
                                                                       {{620, 410, 120, 90}, "obj2"}};
    const auto result2 = packer.get_packed_rectangles_with_ids(frame2, 2);
    ASSERT_TRUE(result2.has_value());
    const auto packed2_obj1 = result2->packed_rectangles[0];
    const auto packed2_obj2 = result2->packed_rectangles[1];

    EXPECT_EQ(packed2_obj1.left - packed0_obj1.left, 20);
    EXPECT_EQ(packed2_obj1.top - packed0_obj1.top, 10);
    EXPECT_EQ(packed2_obj2.left - packed0_obj2.left, 20);
    EXPECT_EQ(packed2_obj2.top - packed0_obj2.top, 10);
}

TEST_F(LowDelayPackerCtuTest, ReappearingRoiReusesReservedSlot) {
    low_delay_packer packer(1920, 1080, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {
            {{100, 100, 80, 60}, "obj1"},
            {{400, 200, 60, 50}, "obj2"},
    };
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    ASSERT_EQ(result0->packed_rectangles.size(), 2U);
    const sf::rectangle obj1_frame0 = result0->packed_rectangles[0];

    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {
            {{400, 200, 60, 50}, "obj2"},
    };
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());

    const std::vector<std::pair<sf::rectangle, std::string>> frame2 = {
            {{100, 100, 80, 60}, "obj1"},
            {{400, 200, 60, 50}, "obj2"},
    };
    const auto result2 = packer.get_packed_rectangles_with_ids(frame2, 2);
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2->packed_rectangles.size(), 2U);

    EXPECT_EQ(result2->packed_rectangles[0].left, obj1_frame0.left);
    EXPECT_EQ(result2->packed_rectangles[0].top, obj1_frame0.top);
    EXPECT_EQ(result2->packed_rectangles[0].width, obj1_frame0.width);
    EXPECT_EQ(result2->packed_rectangles[0].height, obj1_frame0.height);
}

TEST_F(LowDelayPackerCtuTest, ReservationExpiresAndFreedSlotIsReusedByDifferentObject) {
    low_delay_packer packer(256, 256, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {
            {{10, 10, 20, 20}, "obj_a"},
    };
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    ASSERT_EQ(result0->packed_rectangles.size(), 1U);
    const auto obj_a_ctu = compute_ctu_rectangle(result0->packed_rectangles[0], 64);
    EXPECT_EQ(obj_a_ctu.left, 0);
    EXPECT_EQ(obj_a_ctu.top, 0);

    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {
            {{10, 10, 20, 20}, "obj_c"},
    };
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());
    const auto obj_c_ctu = compute_ctu_rectangle(result1->packed_rectangles[0], 64);
    EXPECT_NE(obj_c_ctu.left, 0) << "New object must not occupy reserved slot immediately after disappearance.";

    for (int frame_number = 2; frame_number <= 5; ++frame_number) {
        const auto result = packer.get_packed_rectangles_with_ids(frame1, frame_number);
        ASSERT_TRUE(result.has_value());
    }

    const std::vector<std::pair<sf::rectangle, std::string>> frame6 = {
            {{10, 10, 20, 20}, "obj_d"},
    };
    const auto result6 = packer.get_packed_rectangles_with_ids(frame6, 6);
    ASSERT_TRUE(result6.has_value());
    const auto obj_d_ctu = compute_ctu_rectangle(result6->packed_rectangles[0], 64);
    EXPECT_EQ(obj_d_ctu.left, 0);
    EXPECT_EQ(obj_d_ctu.top, 0);
}

TEST_F(LowDelayPackerCtuTest, NumFramesFiveUsesSlidingAccumulationWindow) {
    low_delay_packer packer(1920, 1080, 64, 5);

    std::optional<pack_result> result;
    for (int frame_number = 0; frame_number <= 4; ++frame_number) {
        const std::vector<std::pair<sf::rectangle, std::string>> frame = {
                {{frame_number * 10, 0, 10, 10}, "obj1"},
        };
        result = packer.get_packed_rectangles_with_ids(frame, frame_number);
        ASSERT_TRUE(result.has_value());
    }

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->sorted_source_rectangles.size(), 1U);
    EXPECT_EQ(result->sorted_source_rectangles[0], sf::rectangle(0, 0, 50, 10));

    const std::vector<std::pair<sf::rectangle, std::string>> frame5 = {
            {{50, 0, 10, 10}, "obj1"},
    };
    const auto result5 = packer.get_packed_rectangles_with_ids(frame5, 5);
    ASSERT_TRUE(result5.has_value());
    ASSERT_EQ(result5->sorted_source_rectangles.size(), 1U);
    EXPECT_EQ(result5->sorted_source_rectangles[0], sf::rectangle(10, 0, 50, 10));
}

TEST_F(LowDelayPackerCtuTest, NearCapacityPackingMaintainsCtuNonOverlapAndOverflowFails) {
    low_delay_packer packer(256, 256, 64, 1);

    std::vector<std::pair<sf::rectangle, std::string>> rois_16;
    rois_16.reserve(16);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int index = row * 4 + col;
            rois_16.emplace_back(sf::rectangle(col * 64 + 8, row * 64 + 8, 20, 20), "obj_" + std::to_string(index));
        }
    }

    const auto packed_16 = packer.get_packed_rectangles_with_ids(rois_16, 0);
    ASSERT_TRUE(packed_16.has_value());
    ASSERT_EQ(packed_16->packed_rectangles.size(), 16U);

    for (size_t left_index = 0; left_index < packed_16->packed_rectangles.size(); ++left_index) {
        const auto left_ctu = compute_ctu_rectangle(packed_16->packed_rectangles[left_index], 64);
        for (size_t right_index = left_index + 1; right_index < packed_16->packed_rectangles.size(); ++right_index) {
            const auto right_ctu = compute_ctu_rectangle(packed_16->packed_rectangles[right_index], 64);
            EXPECT_FALSE(ctu_rectangles_overlap(left_ctu, right_ctu))
                    << "CTU overlap between near-capacity ROIs " << left_index << " and " << right_index;
        }
    }

    low_delay_packer overflow_packer(256, 256, 64, 1);
    auto rois_17 = rois_16;
    // This ROI does not overlap the first one in source space, but maps to the same CTU cell.
    rois_17.emplace_back(sf::rectangle(40, 40, 20, 20), "obj_overflow");
    const auto packed_17 = overflow_packer.get_packed_rectangles_with_ids(rois_17, 0);
    EXPECT_FALSE(packed_17.has_value()) << "Packing should fail once CTU grid capacity is exceeded.";
}

TEST_F(LowDelayPackerCtuTest, LargeMultiCtuRoiStaysSeparatedFromNeighborsAcrossFrames) {
    low_delay_packer packer(640, 480, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {
            {{120, 90, 158, 88}, "large_roi"}, // spans multiple CTUs in both axes
            {{20, 30, 40, 36}, "small_left"},
            {{500, 350, 52, 44}, "small_right"},
    };
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    ASSERT_EQ(result0->packed_rectangles.size(), frame0.size());

    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {
            {{130, 100, 158, 88}, "large_roi"}, // translation to stress correspondence + CTU occupancy
            {{24, 34, 40, 36}, "small_left"},
            {{506, 356, 52, 44}, "small_right"},
    };
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1->packed_rectangles.size(), frame1.size());

    const auto assert_result_has_disjoint_ctu = [](const pack_result &result) {
        bool observed_large_multi_ctu = false;
        for (size_t left_index = 0; left_index < result.packed_rectangles.size(); ++left_index) {
            const auto left_ctu = compute_ctu_rectangle(result.packed_rectangles[left_index], 64);
            if (left_ctu.width > 64 || left_ctu.height > 64) {
                observed_large_multi_ctu = true;
            }
            for (size_t right_index = left_index + 1; right_index < result.packed_rectangles.size(); ++right_index) {
                const auto right_ctu = compute_ctu_rectangle(result.packed_rectangles[right_index], 64);
                EXPECT_FALSE(ctu_rectangles_overlap(left_ctu, right_ctu))
                        << "CTU overlap between packed ROIs " << left_index << " and " << right_index;
            }
        }
        EXPECT_TRUE(observed_large_multi_ctu) << "Expected at least one multi-CTU packed ROI in the test frame.";
    };

    assert_result_has_disjoint_ctu(result0.value());
    assert_result_has_disjoint_ctu(result1.value());
}

TEST_F(LowDelayPackerCtuTest, LargeRoiGrowthDoesNotIntrudeOnNeighborCtuCells) {
    low_delay_packer packer(640, 480, 64, 1);

    const std::vector<std::pair<sf::rectangle, std::string>> frame0 = {
            {{100, 80, 130, 86}, "large_roi"},
            {{290, 80, 58, 42}, "neighbor_roi"},
    };
    const auto result0 = packer.get_packed_rectangles_with_ids(frame0, 0);
    ASSERT_TRUE(result0.has_value());
    ASSERT_EQ(result0->packed_rectangles.size(), 2U);

    const std::vector<std::pair<sf::rectangle, std::string>> frame1 = {
            {{100, 80, 180, 120}, "large_roi"}, // growth forces larger CTU envelope
            {{290, 80, 58, 42}, "neighbor_roi"},
    };
    const auto result1 = packer.get_packed_rectangles_with_ids(frame1, 1);
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1->packed_rectangles.size(), 2U);

    const auto large_ctu = compute_ctu_rectangle(result1->packed_rectangles[0], 64);
    const auto neighbor_ctu = compute_ctu_rectangle(result1->packed_rectangles[1], 64);
    EXPECT_TRUE(large_ctu.width > 64 || large_ctu.height > 64)
            << "Large ROI should occupy multiple CTU cells after growth.";
    EXPECT_FALSE(ctu_rectangles_overlap(large_ctu, neighbor_ctu))
            << "Neighbor ROI CTU cells must remain disjoint from grown large ROI.";
}
