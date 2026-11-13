// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <vector>
#include "nal_utils.h"
#include "packed_regions_info.h"

// Test fixture for nal_utils tests
struct nal_utils_test : ::testing::Test {};

// Test case for creating a SEI NAL unit
TEST_F(nal_utils_test, create_sei_nal_unit) {
    auto pri_data = sf::packed_regions_info{};
    pri_data.cancel_flag = false;
    pri_data.persistence_flag = false;
    pri_data.multilayer_flag = false;
    pri_data.use_max_dimensions_flag = false;
    pri_data.log2_unit_size = 0;
    pri_data.region_size_len_minus1 = 0;
    pri_data.region_id_present_flag = false;
    pri_data.target_pic_params_present_flag = false;
    pri_data.resampling_ratios.push_back({0, 0, true});
    pri_data.regions.push_back({std::nullopt, std::nullopt, true});

    const auto sei_nal_unit = sf::create_sei_nal_unit(pri_data);

    // NAL unit header for a prefix SEI message is 0x4E01
    // NAL unit type is 39 (PREFIX_SEI_NUT)
    // nuh_layer_id = 0, nuh_temporal_id_plus1 = 1
    ASSERT_GE(sei_nal_unit.size(), 2);
    EXPECT_EQ(sei_nal_unit[0], 0x4E); // NAL unit header
    EXPECT_EQ(sei_nal_unit[1], 0x01); // NAL unit header

    // SEI message header: payloadType = 226 (packed regions info), payloadSize > 0
    ASSERT_GE(sei_nal_unit.size(), 4);
    EXPECT_EQ(sei_nal_unit[2], 226); // payloadType
    EXPECT_GT(sei_nal_unit[3], 0); // payloadSize
}

// Test case for injecting a SEI message
TEST_F(nal_utils_test, inject_sei_message) {
    // A fake H.265 access unit with a slice NAL unit (NAL unit type 1)
    // Start code + NAL unit header
    std::vector<uint8_t> frame_data = {0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0xAB, 0xCD};

    // A fake SEI NAL unit
    const std::vector<uint8_t> sei_nal_unit = {0x4E, 0x01, 147, 1, 0xFF};

    const auto new_frame = sf::inject_sei_message(frame_data.data(), frame_data.size(), sei_nal_unit);

    // Expected result: SEI NAL unit injected before the slice NAL unit
    std::vector<uint8_t> expected_frame = {
            0x00, 0x00, 0x01, // Start code for SEI
            0x4E, 0x01, 147,  1,    0xFF, // SEI NAL unit
            0x00, 0x00, 0x00, 0x01, // Start code for slice
            0x02, 0x01, 0xAB, 0xCD // Slice NAL unit
    };

    ASSERT_EQ(new_frame.size(), frame_data.size() + sei_nal_unit.size() + 3);
}

TEST_F(nal_utils_test, create_generic_eoi_sei_nal_unit) {
    const std::vector<uint8_t> eoi_payload = {0x12, 0x34};
    const auto sei_nal_unit = sf::create_sei_nal_unit(215, eoi_payload);

    ASSERT_GE(sei_nal_unit.size(), 6);
    EXPECT_EQ(sei_nal_unit[0], 0x4E); // PREFIX_SEI_NUT header
    EXPECT_EQ(sei_nal_unit[1], 0x01);
    EXPECT_EQ(sei_nal_unit[2], 0xD7); // payloadType 215
    EXPECT_EQ(sei_nal_unit[3], 0x02); // payload size
    EXPECT_EQ(sei_nal_unit[4], 0x12);
    EXPECT_EQ(sei_nal_unit[5], 0x34);
}

TEST_F(nal_utils_test, detects_hevc_irap_vcl_nal) {
    // CRA_NUT (type 21) at layer 0.
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0xAA, 0xBB,
    };

    EXPECT_TRUE(sf::contains_hevc_irap_vcl_nal(irap_frame.data(), irap_frame.size()));
}

TEST_F(nal_utils_test, ignores_non_irap_vcl_nal_for_irap_detection) {
    // TRAIL_R (type 1) at layer 0.
    const std::vector<uint8_t> non_irap_frame = {
            0x00, 0x00, 0x01, 0x02, 0x01, 0xAA, 0xBB,
    };

    EXPECT_FALSE(sf::contains_hevc_irap_vcl_nal(non_irap_frame.data(), non_irap_frame.size()));
}
