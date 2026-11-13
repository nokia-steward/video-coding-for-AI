// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

// packed_regions_info_test.cpp
#include <gtest/gtest.h>
#include "packed_regions_info.h"

// Test fixture for packed_regions_info tests
struct packed_regions_info_test : ::testing::Test {
protected:
    // Per-test-suite set-up.
    static void SetUpTestSuite() {}

    // Per-test-suite tear-down.
    static void TearDownTestSuite() {}

    // You can define per-test set-up and tear-down logic as well.
    void SetUp() override {}

    void TearDown() override {}
};

// Test case for when the cancel_flag is true.
TEST_F(packed_regions_info_test, cancel_flag_true) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = true;

    // Serialize the data
    const auto buffer = sf::write_packed_regions_info(original_data);

    // Deserialize the data
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    // Verify the data
    ASSERT_TRUE(deserialized_data.cancel_flag);
    ASSERT_FALSE(deserialized_data.persistence_flag.has_value());
}

// Test case for a full message with all optional fields.
TEST_F(packed_regions_info_test, full_message) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = true;
    original_data.multilayer_flag = true;
    original_data.use_max_dimensions_flag = true;
    original_data.log2_unit_size = 3;
    original_data.region_size_len_minus1 = 15;
    original_data.region_id_present_flag = true;
    original_data.target_pic_params_present_flag = true;
    original_data.target_pic_width_minus1 = 1919;
    original_data.target_pic_height_minus1 = 1079;

    original_data.resampling_ratios.push_back({1, 1, false, 2, 2}); // spec index 1
    original_data.regions.push_back({0, 0, false, 10, 20, 30, 40, 1, 5, 15});

    // Serialize the data
    const auto buffer = sf::write_packed_regions_info(original_data);

    // Deserialize the data
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    // Verify the data
    ASSERT_EQ(original_data.cancel_flag, deserialized_data.cancel_flag);
    ASSERT_EQ(original_data.persistence_flag, deserialized_data.persistence_flag);
    ASSERT_EQ(original_data.multilayer_flag, deserialized_data.multilayer_flag);
    ASSERT_EQ(original_data.use_max_dimensions_flag, deserialized_data.use_max_dimensions_flag);
    ASSERT_EQ(original_data.log2_unit_size, deserialized_data.log2_unit_size);
    ASSERT_EQ(original_data.region_size_len_minus1, deserialized_data.region_size_len_minus1);
    ASSERT_EQ(original_data.region_id_present_flag, deserialized_data.region_id_present_flag);
    ASSERT_EQ(original_data.target_pic_params_present_flag, deserialized_data.target_pic_params_present_flag);
    ASSERT_EQ(original_data.target_pic_width_minus1, deserialized_data.target_pic_width_minus1);
    ASSERT_EQ(original_data.target_pic_height_minus1, deserialized_data.target_pic_height_minus1);

    ASSERT_EQ(original_data.resampling_ratios.size(), deserialized_data.resampling_ratios.size());
    ASSERT_EQ(deserialized_data.inferred_resampling_ratio.width_num_minus1, 0U);
    ASSERT_EQ(deserialized_data.inferred_resampling_ratio.width_denom_minus1, 0U);
    ASSERT_EQ(original_data.resampling_ratios[0].width_num_minus1,
              deserialized_data.resampling_ratios[0].width_num_minus1);
    ASSERT_EQ(original_data.resampling_ratios[0].height_denom_minus1,
              deserialized_data.resampling_ratios[0].height_denom_minus1);

    ASSERT_EQ(original_data.regions.size(), deserialized_data.regions.size());
    ASSERT_EQ(original_data.regions[0].id, deserialized_data.regions[0].id);
    ASSERT_EQ(original_data.regions[0].target_region_top_left_in_units_y,
              deserialized_data.regions[0].target_region_top_left_in_units_y);
}

// Test case for a message with no optional fields.
TEST_F(packed_regions_info_test, no_optional_fields) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = false;
    original_data.multilayer_flag = false;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 0;
    original_data.region_id_present_flag = false;
    original_data.target_pic_params_present_flag = false;
    original_data.regions.push_back({std::nullopt, std::nullopt, true});

    // Serialize the data
    const auto buffer = sf::write_packed_regions_info(original_data);

    // Deserialize the data
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    // Verify the data
    ASSERT_EQ(original_data.cancel_flag, deserialized_data.cancel_flag);
    ASSERT_EQ(original_data.persistence_flag, deserialized_data.persistence_flag);
    ASSERT_EQ(original_data.multilayer_flag, deserialized_data.multilayer_flag);
    ASSERT_EQ(original_data.use_max_dimensions_flag, deserialized_data.use_max_dimensions_flag);
    ASSERT_EQ(original_data.log2_unit_size, deserialized_data.log2_unit_size);
    ASSERT_EQ(original_data.region_size_len_minus1, deserialized_data.region_size_len_minus1);
    ASSERT_EQ(original_data.region_id_present_flag, deserialized_data.region_id_present_flag);
    ASSERT_EQ(original_data.target_pic_params_present_flag, deserialized_data.target_pic_params_present_flag);
}

// Test case for multiple regions and resampling ratios.
TEST_F(packed_regions_info_test, multiple_regions_and_ratios) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = true;
    original_data.multilayer_flag = true;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 2;
    original_data.region_size_len_minus1 = 7;
    original_data.region_id_present_flag = true;
    original_data.target_pic_params_present_flag = false;

    original_data.resampling_ratios.push_back({1, 1, false, 2, 2}); // spec index 1
    original_data.regions.push_back({0, 0, false, 1, 2, 3, 4, 0});
    original_data.regions.push_back({1, 1, false, 5, 6, 7, 8, 1});

    // Serialize and deserialize
    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    // Verify
    ASSERT_EQ(original_data.resampling_ratios.size(), deserialized_data.resampling_ratios.size());
    ASSERT_EQ(original_data.regions.size(), deserialized_data.regions.size());
    ASSERT_EQ(deserialized_data.regions[0].resampling_ratio_idx.value_or(0), 0U);
    ASSERT_EQ(original_data.regions[1].resampling_ratio_idx, deserialized_data.regions[1].resampling_ratio_idx);
}

// Test case for round-trip serialization and deserialization.
TEST_F(packed_regions_info_test, round_trip) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = true;
    original_data.multilayer_flag = true;
    original_data.use_max_dimensions_flag = true;
    original_data.log2_unit_size = 3;
    original_data.region_size_len_minus1 = 15;
    original_data.region_id_present_flag = true;
    original_data.target_pic_params_present_flag = true;
    original_data.target_pic_width_minus1 = 1919;
    original_data.target_pic_height_minus1 = 1079;

    original_data.resampling_ratios.push_back({1, 1, false, 2, 2}); // spec index 1
    original_data.regions.push_back({0, 0, false, 10, 20, 30, 40, 1, 5, 15});

    // Serialize and deserialize
    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    // Compare the two structs
    ASSERT_EQ(original_data.cancel_flag, deserialized_data.cancel_flag);
    ASSERT_EQ(original_data.persistence_flag, deserialized_data.persistence_flag);
    ASSERT_EQ(original_data.regions.size(), deserialized_data.regions.size());
    ASSERT_EQ(original_data.regions[0].id, deserialized_data.regions[0].id);
}

// Test case for flag dependencies
TEST_F(packed_regions_info_test, flag_dependencies) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = true;
    original_data.multilayer_flag = false; // Test this branch
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 2;
    original_data.region_size_len_minus1 = 4;
    original_data.region_id_present_flag = false; // Test this branch
    original_data.target_pic_params_present_flag = false; // Test this branch

    // No target pic params, so these should not be serialized
    original_data.target_pic_width_minus1 = 1919;
    original_data.target_pic_height_minus1 = 1079;

    original_data.regions.push_back({std::nullopt, std::nullopt, false, 1, 2, 3, 4, std::nullopt});


    // Serialize and deserialize
    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    // Verify the data
    ASSERT_EQ(original_data.cancel_flag, deserialized_data.cancel_flag);
    ASSERT_EQ(original_data.persistence_flag, deserialized_data.persistence_flag);
    ASSERT_EQ(original_data.multilayer_flag, deserialized_data.multilayer_flag);
    ASSERT_EQ(original_data.use_max_dimensions_flag, deserialized_data.use_max_dimensions_flag);
    ASSERT_EQ(original_data.log2_unit_size, deserialized_data.log2_unit_size);
    ASSERT_EQ(original_data.region_size_len_minus1, deserialized_data.region_size_len_minus1);
    ASSERT_EQ(original_data.region_id_present_flag, deserialized_data.region_id_present_flag);
    ASSERT_EQ(original_data.target_pic_params_present_flag, deserialized_data.target_pic_params_present_flag);

    // These should not have been read from the buffer
    ASSERT_FALSE(deserialized_data.target_pic_width_minus1.has_value());
    ASSERT_FALSE(deserialized_data.target_pic_height_minus1.has_value());

    ASSERT_EQ(deserialized_data.regions.size(), 1);
    ASSERT_FALSE(deserialized_data.regions[0].id.has_value());
}

TEST_F(packed_regions_info_test, mixed_identity_and_signaled_ratio_indexes) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = true;
    original_data.multilayer_flag = false;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 8;
    original_data.region_id_present_flag = true;
    original_data.target_pic_params_present_flag = false;

    // One signaled ratio exists at spec index 1. Index 0 remains inferred 1:1.
    original_data.resampling_ratios.push_back({3, 1, false, 1, 0});
    original_data.regions.push_back({0, std::nullopt, std::nullopt, 0, 0, 31, 31, 0});
    original_data.regions.push_back({1, std::nullopt, std::nullopt, 32, 0, 15, 15, 1});

    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    ASSERT_EQ(deserialized_data.resampling_ratios.size(), 1U);
    ASSERT_EQ(deserialized_data.regions.size(), 2U);
    ASSERT_EQ(deserialized_data.inferred_resampling_ratio.width_num_minus1, 0U);
    ASSERT_EQ(deserialized_data.inferred_resampling_ratio.width_denom_minus1, 0U);
    ASSERT_EQ(deserialized_data.regions[0].resampling_ratio_idx.value_or(0), 0U);
    ASSERT_EQ(deserialized_data.regions[1].resampling_ratio_idx.value_or(0), 1U);
}

// --- Spec 8.32.2 value-range and conformance tests (public API only) ---

// pri_num_regions_minus1 in 0..255: round-trip with minimum (0 => 1 region).
TEST_F(packed_regions_info_test, round_trip_pri_num_regions_minus1_zero_one_region) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = false;
    original_data.multilayer_flag = false;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 0;
    original_data.region_id_present_flag = false;
    original_data.target_pic_params_present_flag = false;
    original_data.regions.push_back({std::nullopt, std::nullopt, false, 0, 0, 0, 0, std::nullopt});

    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    ASSERT_EQ(deserialized_data.regions.size(), 1U);
}

// pri_region_id in 0..1023 and unique: round-trip with ids 0 and 1023.
TEST_F(packed_regions_info_test, round_trip_region_id_spec_range_0_and_1023) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = false;
    original_data.multilayer_flag = false;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 0;
    original_data.region_id_present_flag = true;
    original_data.target_pic_params_present_flag = false;
    original_data.regions.push_back({0, std::nullopt, std::nullopt, 0, 0, 0, 0, std::nullopt});
    original_data.regions.push_back({1023, std::nullopt, std::nullopt, 1, 0, 0, 0, std::nullopt});

    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    ASSERT_EQ(deserialized_data.regions.size(), 2U);
    ASSERT_TRUE(deserialized_data.regions[0].id.has_value());
    ASSERT_TRUE(deserialized_data.regions[1].id.has_value());
    EXPECT_EQ(deserialized_data.regions[0].id.value(), 0U);
    EXPECT_EQ(deserialized_data.regions[1].id.value(), 1023U);
    EXPECT_NE(deserialized_data.regions[0].id.value(), deserialized_data.regions[1].id.value());
}

// pri_region_layer_id in 0..2047: round-trip when multilayer_flag is true.
TEST_F(packed_regions_info_test, round_trip_region_layer_id_spec_range_0_and_2047) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = false;
    original_data.multilayer_flag = true;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 0;
    original_data.region_id_present_flag = false;
    original_data.target_pic_params_present_flag = false;
    original_data.regions.push_back({std::nullopt, 0, false, 0, 0, 0, 0, std::nullopt});
    original_data.regions.push_back({std::nullopt, 2047, false, 1, 0, 0, 0, std::nullopt});

    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    ASSERT_EQ(deserialized_data.regions.size(), 2U);
    ASSERT_TRUE(deserialized_data.regions[0].layer_id.has_value());
    ASSERT_TRUE(deserialized_data.regions[1].layer_id.has_value());
    EXPECT_EQ(deserialized_data.regions[0].layer_id.value(), 0U);
    EXPECT_EQ(deserialized_data.regions[1].layer_id.value(), 2047U);
}

// Resampling ratio components in 0..65535: round-trip at boundaries.
TEST_F(packed_regions_info_test, round_trip_resampling_ratio_components_spec_range) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = false;
    original_data.multilayer_flag = false;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 0;
    original_data.region_id_present_flag = false;
    original_data.target_pic_params_present_flag = false;
    // Spec: width_num_minus1 and width_denom_minus1 in 0..65535. Fixed aspect so height not signaled.
    original_data.resampling_ratios.push_back({0, 65535, true, std::nullopt, std::nullopt});
    original_data.resampling_ratios.push_back({65535, 0, true, std::nullopt, std::nullopt});
    original_data.regions.push_back({std::nullopt, std::nullopt, false, 0, 0, 0, 0, 0});
    original_data.regions.push_back({std::nullopt, std::nullopt, false, 1, 0, 0, 0, 1});

    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    ASSERT_EQ(deserialized_data.resampling_ratios.size(), 2U);
    EXPECT_EQ(deserialized_data.resampling_ratios[0].width_num_minus1, 0U);
    EXPECT_EQ(deserialized_data.resampling_ratios[0].width_denom_minus1, 65535U);
    EXPECT_EQ(deserialized_data.resampling_ratios[1].width_num_minus1, 65535U);
    EXPECT_EQ(deserialized_data.resampling_ratios[1].width_denom_minus1, 0U);
}

// pri_num_resampling_ratios_minus1 in 0..pri_num_regions_minus1: num_ratios equal to num_regions (2 regions, 2 ratios).
TEST_F(packed_regions_info_test, round_trip_num_resampling_ratios_equal_to_num_regions) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = false;
    original_data.multilayer_flag = false;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 0;
    original_data.region_id_present_flag = false;
    original_data.target_pic_params_present_flag = false;
    original_data.resampling_ratios.push_back({1, 1, true, std::nullopt, std::nullopt});
    original_data.regions.push_back({std::nullopt, std::nullopt, false, 0, 0, 0, 0, 0});
    original_data.regions.push_back({std::nullopt, std::nullopt, false, 1, 0, 0, 0, 1});

    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    ASSERT_EQ(deserialized_data.regions.size(), 2U);
    ASSERT_EQ(deserialized_data.resampling_ratios.size(), 1U);
    EXPECT_EQ(deserialized_data.regions[0].resampling_ratio_idx.value_or(0), 0U);
    EXPECT_EQ(deserialized_data.regions[1].resampling_ratio_idx.value_or(0), 1U);
}

// Target picture dimensions: valid 4:2:0 alignment (SubWidthC=2, SubHeightC=2) round-trips.
TEST_F(packed_regions_info_test, round_trip_target_pic_dimensions_4_2_0_aligned) {
    auto original_data = sf::packed_regions_info{};
    original_data.cancel_flag = false;
    original_data.persistence_flag = false;
    original_data.multilayer_flag = false;
    original_data.use_max_dimensions_flag = false;
    original_data.log2_unit_size = 0;
    original_data.region_size_len_minus1 = 0;
    original_data.region_id_present_flag = false;
    original_data.target_pic_params_present_flag = true;
    original_data.target_pic_width_minus1 = 1919;
    original_data.target_pic_height_minus1 = 1079;
    original_data.regions.push_back({std::nullopt, std::nullopt, false, 0, 0, 0, 0, std::nullopt, 0, 0});

    const auto buffer = sf::write_packed_regions_info(original_data);
    const auto deserialized_data = sf::read_packed_regions_info(buffer);

    ASSERT_TRUE(deserialized_data.target_pic_width_minus1.has_value());
    ASSERT_TRUE(deserialized_data.target_pic_height_minus1.has_value());
    EXPECT_EQ(deserialized_data.target_pic_width_minus1.value(), 1919U);
    EXPECT_EQ(deserialized_data.target_pic_height_minus1.value(), 1079U);
    EXPECT_EQ((deserialized_data.target_pic_width_minus1.value() + 1) % 2, 0U);
    EXPECT_EQ((deserialized_data.target_pic_height_minus1.value() + 1) % 2, 0U);
}
