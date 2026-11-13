// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "luma_adaptation_math.h"

#include <gtest/gtest.h>

TEST(luma_adaptation_math_test, derive_ratio_percentage_respects_sign_convention) {
    EXPECT_EQ(sf::derive_luma_ratio_percentage(true, 50U), 50);
    EXPECT_EQ(sf::derive_luma_ratio_percentage(false, 20U), 120);
}

TEST(luma_adaptation_math_test, derived_signal_for_half_scale_matches_expected_values) {
    const auto signal = sf::derive_luma_adaptation_signal(0.5);
    EXPECT_TRUE(signal.ratio_luma_sign_flag);
    EXPECT_EQ(signal.ratio_luma_value, 50U);
    EXPECT_EQ(signal.ratio_percentage, 50);
    EXPECT_EQ(signal.backscale_ratio_value, 100U);
}

TEST(luma_adaptation_math_test, derived_multiplier_tracks_inverse_downscale_factor) {
    constexpr double scales[] = {0.5, 0.65, 0.7, 0.9, 1.0};
    for (const double scale: scales) {
        const auto signal = sf::derive_luma_adaptation_signal(scale);
        const auto ratio_percentage =
                sf::derive_luma_ratio_percentage(signal.ratio_luma_sign_flag, signal.ratio_luma_value);
        const auto multiplier = sf::compute_luma_backscale_multiplier(ratio_percentage, signal.backscale_ratio_value);
        ASSERT_TRUE(multiplier.has_value());
        EXPECT_NEAR(*multiplier, 1.0 / scale, 1e-6);
    }
}

TEST(luma_adaptation_math_test, derive_signal_with_explicit_upscale_encodes_multiplier) {
    constexpr double d = 0.5;
    constexpr double u = 1.5; // not full backscale 1/d = 2.0
    const auto signal = sf::derive_luma_adaptation_signal(d, u);
    const int ratio_percentage = sf::derive_luma_ratio_percentage(signal.ratio_luma_sign_flag, signal.ratio_luma_value);
    const auto multiplier = sf::compute_luma_backscale_multiplier(ratio_percentage, signal.backscale_ratio_value);
    ASSERT_TRUE(multiplier.has_value());
    EXPECT_NEAR(*multiplier, u, 0.02); // quantization to 0..127
}

TEST(luma_adaptation_math_test, derive_signal_with_nullopt_upscale_equals_single_arg) {
    constexpr double d = 0.7;
    const auto with_opt = sf::derive_luma_adaptation_signal(d, std::nullopt);
    const auto without_opt = sf::derive_luma_adaptation_signal(d);
    EXPECT_EQ(with_opt.ratio_luma_sign_flag, without_opt.ratio_luma_sign_flag);
    EXPECT_EQ(with_opt.ratio_luma_value, without_opt.ratio_luma_value);
    EXPECT_EQ(with_opt.ratio_percentage, without_opt.ratio_percentage);
    EXPECT_EQ(with_opt.backscale_ratio_value, without_opt.backscale_ratio_value);
}

TEST(luma_adaptation_math_test, compute_multiplier_rejects_invalid_inputs) {
    EXPECT_FALSE(sf::compute_luma_backscale_multiplier(0, 100).has_value());
    EXPECT_FALSE(sf::compute_luma_backscale_multiplier(50, 0).has_value());
    EXPECT_FALSE(sf::compute_luma_backscale_multiplier(-1, 100).has_value());
    EXPECT_FALSE(sf::compute_luma_backscale_multiplier(50, -1).has_value());
}
