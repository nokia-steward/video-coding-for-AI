// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/roi/low_delay_packer/ordering.h"

namespace {
    std::vector<stable_rectangle> make_rectangles_for_ordering() {
        return {
                stable_rectangle(0, 0, 4, 4, "a"), // area 16
                stable_rectangle(0, 0, 5, 3, "b"), // area 15
                stable_rectangle(0, 0, 2, 6, "c"), // area 12
                stable_rectangle(0, 0, 1, 1, "d"), // area 1
        };
    }
} // namespace

TEST(low_delay_ordering_test, sort_by_area_desc_and_asc_return_expected_sequence) {
    const auto input = make_rectangles_for_ordering();

    const auto desc = get_sorted_by_area_desc(input);
    ASSERT_EQ(desc.size(), input.size());
    EXPECT_EQ(desc[0].id, "a");
    EXPECT_EQ(desc[1].id, "b");
    EXPECT_EQ(desc[2].id, "c");
    EXPECT_EQ(desc[3].id, "d");

    const auto asc = get_sorted_by_area_asc(input);
    ASSERT_EQ(asc.size(), input.size());
    EXPECT_EQ(asc[0].id, "d");
    EXPECT_EQ(asc[1].id, "c");
    EXPECT_EQ(asc[2].id, "b");
    EXPECT_EQ(asc[3].id, "a");

    // Ensure helpers do not mutate the original order.
    EXPECT_EQ(input[0].id, "a");
    EXPECT_EQ(input[1].id, "b");
    EXPECT_EQ(input[2].id, "c");
    EXPECT_EQ(input[3].id, "d");
}

TEST(low_delay_ordering_test, sort_by_width_and_height_desc_return_expected_sequence) {
    const std::vector<stable_rectangle> input = {
            stable_rectangle(0, 0, 3, 4, "w3_h4"),
            stable_rectangle(0, 0, 5, 1, "w5_h1"),
            stable_rectangle(0, 0, 1, 6, "w1_h6"),
            stable_rectangle(0, 0, 2, 2, "w2_h2"),
    };

    const auto width_desc = get_sorted_by_width_desc(input);
    ASSERT_EQ(width_desc.size(), input.size());
    EXPECT_EQ(width_desc[0].id, "w5_h1");
    EXPECT_EQ(width_desc[1].id, "w3_h4");
    EXPECT_EQ(width_desc[2].id, "w2_h2");
    EXPECT_EQ(width_desc[3].id, "w1_h6");

    const auto height_desc = get_sorted_by_height_desc(input);
    ASSERT_EQ(height_desc.size(), input.size());
    EXPECT_EQ(height_desc[0].id, "w1_h6");
    EXPECT_EQ(height_desc[1].id, "w3_h4");
    EXPECT_EQ(height_desc[2].id, "w2_h2");
    EXPECT_EQ(height_desc[3].id, "w5_h1");
}

TEST(low_delay_ordering_test, index_helpers_return_expected_index_orders) {
    const auto input = make_rectangles_for_ordering();

    const auto original = get_original_order(input);
    const std::vector<size_t> expected_original = {0, 1, 2, 3};
    EXPECT_EQ(original, expected_original);

    const auto desc_indices = get_sorted_by_area_desc_indices(input);
    const std::vector<size_t> expected_desc = {0, 1, 2, 3};
    EXPECT_EQ(desc_indices, expected_desc);

    const auto asc_indices = get_sorted_by_area_asc_indices(input);
    const std::vector<size_t> expected_asc = {3, 2, 1, 0};
    EXPECT_EQ(asc_indices, expected_asc);

    const auto width_desc_indices = get_sorted_by_width_desc_indices(input);
    const std::vector<size_t> expected_width_desc = {1, 0, 2, 3};
    EXPECT_EQ(width_desc_indices, expected_width_desc);

    const auto height_desc_indices = get_sorted_by_height_desc_indices(input);
    const std::vector<size_t> expected_height_desc = {2, 0, 1, 3};
    EXPECT_EQ(height_desc_indices, expected_height_desc);
}

TEST(low_delay_ordering_test, empty_input_returns_empty_results_for_all_helpers) {
    const std::vector<stable_rectangle> empty;

    EXPECT_TRUE(get_sorted_by_area_desc(empty).empty());
    EXPECT_TRUE(get_sorted_by_area_asc(empty).empty());
    EXPECT_TRUE(get_sorted_by_width_desc(empty).empty());
    EXPECT_TRUE(get_sorted_by_height_desc(empty).empty());
    EXPECT_TRUE(get_original_order(empty).empty());
    EXPECT_TRUE(get_sorted_by_area_desc_indices(empty).empty());
    EXPECT_TRUE(get_sorted_by_area_asc_indices(empty).empty());
    EXPECT_TRUE(get_sorted_by_width_desc_indices(empty).empty());
    EXPECT_TRUE(get_sorted_by_height_desc_indices(empty).empty());
}
