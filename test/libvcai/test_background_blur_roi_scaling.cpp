// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../libvcai/src/background_blur/internal.h"

namespace {
    using sf::rectangle;
    using sf::background_blur_internal::build_roi_mask;
    using sf::background_blur_internal::scale_roi_for_subsampled_plane;
    using sf::background_blur_internal::scale_rois_for_plane;
} // namespace

TEST(background_blur_roi_scaling_test, scale_roi_for_subsampled_plane_rounds_and_clamps) {
    const rectangle roi{3, 5, 5, 7};
    const rectangle scaled = scale_roi_for_subsampled_plane(roi, 2, 2, 8, 6);
    EXPECT_EQ(scaled, rectangle(1, 2, 3, 4));

    const rectangle tail_roi{14, 10, 10, 10};
    const rectangle tail_scaled = scale_roi_for_subsampled_plane(tail_roi, 2, 2, 8, 6);
    EXPECT_EQ(tail_scaled, rectangle(7, 5, 1, 1));
}

TEST(background_blur_roi_scaling_test, scale_rois_for_plane_filters_invalid_scaled_rois) {
    const std::vector<rectangle> rois = {
            rectangle(0, 0, 4, 4),
            rectangle(100, 100, 10, 10),
            rectangle(6, 2, 2, 2),
    };

    const auto scaled = scale_rois_for_plane(rois, 2, 2, 8, 6);
    ASSERT_EQ(scaled.size(), 2U);
    EXPECT_EQ(scaled[0], rectangle(0, 0, 2, 2));
    EXPECT_EQ(scaled[1], rectangle(3, 1, 1, 1));
}

TEST(background_blur_roi_scaling_test, build_roi_mask_marks_pixels_and_full_rows_columns) {
    const std::vector<rectangle> rois = {
            rectangle(1, 0, 3, 3),
            rectangle(0, 1, 4, 1),
    };

    const auto mask = build_roi_mask(4, 3, rois);
    ASSERT_EQ(mask.pixels.size(), 12U);
    ASSERT_EQ(mask.row_all_roi.size(), 3U);
    ASSERT_EQ(mask.col_all_roi.size(), 4U);

    EXPECT_TRUE(mask.contains(1, 0));
    EXPECT_TRUE(mask.contains(3, 2));
    EXPECT_FALSE(mask.contains(0, 0));
    EXPECT_FALSE(mask.contains(-1, 1));
    EXPECT_FALSE(mask.contains(4, 1));

    EXPECT_FALSE(mask.is_row_all_roi(0));
    EXPECT_TRUE(mask.is_row_all_roi(1));
    EXPECT_FALSE(mask.is_row_all_roi(5));

    EXPECT_FALSE(mask.is_column_all_roi(0));
    EXPECT_TRUE(mask.is_column_all_roi(1));
    EXPECT_TRUE(mask.is_column_all_roi(2));
    EXPECT_TRUE(mask.is_column_all_roi(3));
    EXPECT_FALSE(mask.is_column_all_roi(4));
}

TEST(background_blur_roi_scaling_test, build_roi_mask_handles_empty_or_invalid_dimensions) {
    const auto empty_from_no_rois = build_roi_mask(5, 4, {});
    EXPECT_FALSE(empty_from_no_rois.contains(0, 0));
    EXPECT_FALSE(empty_from_no_rois.is_row_all_roi(0));
    EXPECT_FALSE(empty_from_no_rois.is_column_all_roi(0));

    const auto empty_from_invalid_dims = build_roi_mask(-2, 0, {rectangle(0, 0, 1, 1)});
    EXPECT_TRUE(empty_from_invalid_dims.pixels.empty());
    EXPECT_TRUE(empty_from_invalid_dims.row_all_roi.empty());
    EXPECT_TRUE(empty_from_invalid_dims.col_all_roi.empty());
    EXPECT_FALSE(empty_from_invalid_dims.contains(0, 0));
}
