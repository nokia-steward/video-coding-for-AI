// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "encoder_optimization_info.h"

namespace {
    using sf::encoder_optimization_info;
}

TEST(encoder_optimization_info_test, cancel_flag_round_trip_preserves_minimal_payload) {
    encoder_optimization_info input{};
    input.cancel_flag = true;

    const auto encoded = sf::write_encoder_optimization_info(input);
    const auto decoded = sf::read_encoder_optimization_info(encoded);

    EXPECT_TRUE(decoded.cancel_flag);
    EXPECT_FALSE(decoded.persistence_flag.has_value());
    EXPECT_FALSE(decoded.type.has_value());
}

TEST(encoder_optimization_info_test, write_throws_when_required_fields_are_missing) {
    encoder_optimization_info input{};
    input.cancel_flag = false;
    input.persistence_flag = true;

    EXPECT_THROW((void) sf::write_encoder_optimization_info(input), std::runtime_error);
}

TEST(encoder_optimization_info_test, round_trip_preserves_all_enabled_type_fields) {
    encoder_optimization_info input{};
    input.cancel_flag = false;
    input.persistence_flag = true;
    input.for_human_viewing_idc = 1;
    input.for_machine_analysis_idc = 2;
    input.reserved_zero_2bits = 0;
    input.type = static_cast<uint16_t>(
            encoder_optimization_info::type_object_based | encoder_optimization_info::type_temporal_resampling |
            encoder_optimization_info::type_spatial_resampling | encoder_optimization_info::type_privacy_protection);

    input.object_based_idc = 0x02;
    input.quant_threshold_delta = 3U;
    input.pic_quant_object_flag = true;

    input.temporal_resampling_type_flag = true;
    input.num_int_pics = 2U;
    input.src_pic_flag = false;

    input.orig_pic_dimensions_flag = false;
    input.spatial_hor_resampling_type_idc = 1U;
    input.spatial_ver_resampling_type_idc = 2U;

    input.privacy_protection_method_idc = 123U;
    input.privacy_info_type = 7U;

    const auto encoded = sf::write_encoder_optimization_info(input);
    const auto decoded = sf::read_encoder_optimization_info(encoded);

    EXPECT_FALSE(decoded.cancel_flag);
    EXPECT_EQ(decoded.persistence_flag, input.persistence_flag);
    EXPECT_EQ(decoded.for_human_viewing_idc, input.for_human_viewing_idc);
    EXPECT_EQ(decoded.for_machine_analysis_idc, input.for_machine_analysis_idc);
    EXPECT_EQ(decoded.type, input.type);
    EXPECT_EQ(decoded.object_based_idc, input.object_based_idc);
    EXPECT_EQ(decoded.quant_threshold_delta, input.quant_threshold_delta);
    EXPECT_EQ(decoded.pic_quant_object_flag, input.pic_quant_object_flag);
    EXPECT_EQ(decoded.temporal_resampling_type_flag, input.temporal_resampling_type_flag);
    EXPECT_EQ(decoded.num_int_pics, input.num_int_pics);
    EXPECT_EQ(decoded.src_pic_flag, input.src_pic_flag);
    EXPECT_EQ(decoded.orig_pic_dimensions_flag, input.orig_pic_dimensions_flag);
    EXPECT_EQ(decoded.spatial_hor_resampling_type_idc, input.spatial_hor_resampling_type_idc);
    EXPECT_EQ(decoded.spatial_ver_resampling_type_idc, input.spatial_ver_resampling_type_idc);
    EXPECT_EQ(decoded.privacy_protection_method_idc, input.privacy_protection_method_idc);
    EXPECT_EQ(decoded.privacy_info_type, input.privacy_info_type);
}

TEST(encoder_optimization_info_test, to_string_reports_cancel_only_when_cancelled) {
    encoder_optimization_info input{};
    input.cancel_flag = true;
    input.type = 0x2A;

    const std::string text = sf::to_string(input);
    EXPECT_NE(text.find("cancel_flag: true"), std::string::npos);
    EXPECT_EQ(text.find("type:"), std::string::npos);
}

TEST(encoder_optimization_info_test, round_trip_preserves_luma_adaptation_fields) {
    encoder_optimization_info input{};
    input.cancel_flag = false;
    input.persistence_flag = true;
    input.for_human_viewing_idc = 0;
    input.for_machine_analysis_idc = 3;
    input.reserved_zero_2bits = 0;
    input.type = encoder_optimization_info::type_luma_adaptation;
    input.luma_adaptation_idc = 1U;
    input.ratio_luma_value = 27U;
    input.ratio_luma_sign_flag = true;
    input.backscale_ratio_value = 100U;

    const auto encoded = sf::write_encoder_optimization_info(input);
    const auto decoded = sf::read_encoder_optimization_info(encoded);

    EXPECT_EQ(decoded.type, input.type);
    EXPECT_EQ(decoded.luma_adaptation_idc, input.luma_adaptation_idc);
    EXPECT_EQ(decoded.ratio_luma_value, input.ratio_luma_value);
    EXPECT_EQ(decoded.ratio_luma_sign_flag, input.ratio_luma_sign_flag);
    EXPECT_EQ(decoded.backscale_ratio_value, input.backscale_ratio_value);
    EXPECT_FALSE(decoded.display_model.has_value());
}

TEST(encoder_optimization_info_test, write_requires_display_model_for_luma_adaptation_idc_zero) {
    encoder_optimization_info input{};
    input.cancel_flag = false;
    input.persistence_flag = true;
    input.for_human_viewing_idc = 0;
    input.for_machine_analysis_idc = 3;
    input.reserved_zero_2bits = 0;
    input.type = encoder_optimization_info::type_luma_adaptation;
    input.luma_adaptation_idc = 0U;
    input.ratio_luma_value = 10U;
    input.ratio_luma_sign_flag = false;
    input.backscale_ratio_value = 95U;

    EXPECT_THROW((void) sf::write_encoder_optimization_info(input), std::runtime_error);
}
