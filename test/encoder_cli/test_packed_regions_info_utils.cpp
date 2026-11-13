// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

// Tests for the encoder helper create_packed_regions_info: they validate the helper's
// behavior and output shape (e.g. unit counts, resampling ratio assignment), not the
// VSEI PRI syntax/semantics per se. For spec-oriented PRI tests see
// libvcai/test_packed_regions_info.cpp.

#include <gtest/gtest.h>
#include "../../encoder_cli/src/utils/packed_regions_info_utils.h"
#include "../../libvcai/include/packed_regions_info.h"

TEST(PackedRegionsInfoUtilsTest, BasicCreation) {
    std::vector<sf::rectangle> roi_data = {{0, 0, 100, 100}, {100, 100, 200, 200}};
    std::vector<sf::rectangle> packed_rois = {
            {0, 0, 100, 100}, {0, 100, 100, 100} // Resized
    };

    creation_params params = {};
    params.reconstructed_width = 300;
    params.reconstructed_height = 300;
    params.log2_unit_size = 2;

    sf::packed_regions_info pri = create_packed_regions_info(roi_data, packed_rois, params);

    EXPECT_FALSE(pri.cancel_flag);
    EXPECT_TRUE(pri.persistence_flag);
    EXPECT_EQ(pri.target_pic_width_minus1, 299);
    EXPECT_EQ(pri.target_pic_height_minus1, 299);
    EXPECT_EQ(pri.log2_unit_size, 2);
    EXPECT_EQ(pri.regions.size(), 2);
    EXPECT_EQ(pri.resampling_ratios.size(), 1);
    EXPECT_EQ(pri.inferred_resampling_ratio.width_num_minus1, 0U);
    EXPECT_EQ(pri.inferred_resampling_ratio.width_denom_minus1, 0U);
    EXPECT_EQ(pri.regions[0].resampling_ratio_idx.value_or(0), 0U);
    EXPECT_EQ(pri.regions[1].resampling_ratio_idx.value_or(0), 1U);
    EXPECT_EQ(pri.resampling_ratios[0].width_num_minus1, 199U);
    EXPECT_EQ(pri.resampling_ratios[0].width_denom_minus1, 99U);

    // Check first region (no resampling)
    EXPECT_EQ(pri.regions[0].id, 0);
    EXPECT_EQ(pri.regions[0].top_left_in_units_x, 0);
    EXPECT_EQ(pri.regions[0].top_left_in_units_y, 0);
    EXPECT_EQ(pri.regions[0].width_in_units_minus1, 24);
    EXPECT_EQ(pri.regions[0].height_in_units_minus1, 24);
    EXPECT_EQ(pri.regions[0].target_region_top_left_in_units_x, 0);
    EXPECT_EQ(pri.regions[0].target_region_top_left_in_units_y, 0);

    // Check second region (resampled)
    EXPECT_EQ(pri.regions[1].id, 1);
    EXPECT_EQ(pri.regions[1].top_left_in_units_x, 0);
    EXPECT_EQ(pri.regions[1].top_left_in_units_y, 25);
    EXPECT_EQ(pri.regions[1].width_in_units_minus1, 24);
    EXPECT_EQ(pri.regions[1].height_in_units_minus1, 24);
    EXPECT_EQ(pri.regions[1].target_region_top_left_in_units_x, 25);
    EXPECT_EQ(pri.regions[1].target_region_top_left_in_units_y, 25);
}

TEST(PackedRegionsInfoUtilsTest, UsesInferredIndexZeroForIdentityRegions) {
    std::vector<sf::rectangle> roi_data = {{0, 0, 80, 80}, {80, 0, 80, 80}};
    std::vector<sf::rectangle> packed_rois = {
            {0, 0, 80, 80}, // identity ratio -> inferred index 0
            {80, 0, 40, 80} // width resampled -> signaled index 1
    };

    creation_params params = {};
    params.reconstructed_width = 160;
    params.reconstructed_height = 80;
    params.log2_unit_size = 0;

    const sf::packed_regions_info pri = create_packed_regions_info(roi_data, packed_rois, params);

    ASSERT_EQ(pri.resampling_ratios.size(), 1U);
    ASSERT_EQ(pri.regions.size(), 2U);
    EXPECT_EQ(pri.regions[0].resampling_ratio_idx.value_or(0), 0U);
    EXPECT_EQ(pri.regions[1].resampling_ratio_idx.value_or(0), 1U);
}

// Helper output is valid PRI: round-trip (write then read) succeeds and recovers consistent semantics.
TEST(PackedRegionsInfoUtilsTest, CreatePackedRegionsInfoOutputRoundTrips) {
    std::vector<sf::rectangle> roi_data = {{0, 0, 64, 64}, {64, 0, 64, 64}};
    std::vector<sf::rectangle> packed_rois = {{0, 0, 64, 64}, {64, 0, 64, 64}};
    creation_params params = {};
    params.reconstructed_width = 128;
    params.reconstructed_height = 64;
    params.log2_unit_size = 0;

    const sf::packed_regions_info pri = create_packed_regions_info(roi_data, packed_rois, params);
    const std::vector<uint8_t> buffer = sf::write_packed_regions_info(pri);
    const sf::packed_regions_info deserialized = sf::read_packed_regions_info(buffer);

    EXPECT_FALSE(deserialized.cancel_flag);
    EXPECT_EQ(deserialized.regions.size(), pri.regions.size());
    EXPECT_EQ(deserialized.target_pic_width_minus1, pri.target_pic_width_minus1);
    EXPECT_EQ(deserialized.target_pic_height_minus1, pri.target_pic_height_minus1);
    EXPECT_EQ(deserialized.log2_unit_size, pri.log2_unit_size);
    if (!deserialized.regions.empty() && pri.regions.size() == deserialized.regions.size()) {
        EXPECT_EQ(deserialized.regions[0].top_left_in_units_x, pri.regions[0].top_left_in_units_x);
        EXPECT_EQ(deserialized.regions[0].width_in_units_minus1, pri.regions[0].width_in_units_minus1);
    }
}
