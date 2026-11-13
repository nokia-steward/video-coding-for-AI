// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/roi/roi_manager/stabilization.h"

/**
 * @file test_roi_stabilization.cpp
 * @brief Unit tests for ROI stabilization logic.
 */

TEST(RoiStabilizationTest, EmaSmoothingReducesJitter) {
    stabilization_config config{};
    config.enabled = true;
    config.mode = stabilization_mode::ema;
    config.alpha = 0.2f;
    config.alpha_size = 0.2f;
    config.velocity_adaptive_factor = 0.0f;

    stabilized_state state{};
    const sf::rectangle first_raw{100, 100, 50, 50};
    const auto first_result = stabilize_bounds(first_raw, state, config, 0.0f, 0.0f);
    EXPECT_EQ(first_result, first_raw);

    const sf::rectangle second_raw{110, 120, 50, 50};
    const auto second_result = stabilize_bounds(second_raw, state, config, 0.0f, 0.0f);

    EXPECT_EQ(second_result.left, 102);
    EXPECT_EQ(second_result.top, 104);
    EXPECT_EQ(second_result.width, 50);
    EXPECT_EQ(second_result.height, 50);
}

TEST(RoiStabilizationTest, ThresholdIgnoresMicroMovement) {
    stabilization_config config{};
    config.enabled = true;
    config.mode = stabilization_mode::threshold;
    config.min_position_delta = 5;
    config.min_size_delta = 5;

    stabilized_state state{};
    const sf::rectangle first_raw{100, 100, 50, 50};
    (void) stabilize_bounds(first_raw, state, config, 0.0f, 0.0f);

    const sf::rectangle second_raw{103, 104, 50, 50};
    const auto second_result = stabilize_bounds(second_raw, state, config, 0.0f, 0.0f);

    EXPECT_EQ(second_result.left, 100);
    EXPECT_EQ(second_result.top, 100);
    EXPECT_EQ(second_result.width, 50);
    EXPECT_EQ(second_result.height, 50);
}

TEST(RoiStabilizationTest, StationaryObjectsDoNotDrift) {
    stabilization_config config{};
    config.enabled = true;
    config.mode = stabilization_mode::hybrid;
    config.alpha = 0.3f;
    config.alpha_size = 0.2f;
    config.min_position_delta = 1;
    config.min_size_delta = 1;

    stabilized_state state{};
    const sf::rectangle raw_bounds{200, 150, 60, 40};
    (void) stabilize_bounds(raw_bounds, state, config, 0.0f, 0.0f);

    for (int index = 0; index < 5; ++index) {
        const auto result = stabilize_bounds(raw_bounds, state, config, 0.0f, 0.0f);
        EXPECT_EQ(result, raw_bounds);
    }
}

TEST(RoiStabilizationTest, FastMovementRemainsResponsive) {
    stabilization_config config{};
    config.enabled = true;
    config.mode = stabilization_mode::hybrid;
    config.alpha = 0.3f;
    config.alpha_size = 0.2f;
    config.velocity_adaptive_factor = 0.5f;

    stabilized_state state{};
    const sf::rectangle first_raw{100, 100, 50, 50};
    (void) stabilize_bounds(first_raw, state, config, 0.0f, 0.0f);

    const sf::rectangle second_raw{200, 100, 50, 50};
    const auto result = stabilize_bounds(second_raw, state, config, 20.0f, 0.0f);

    EXPECT_EQ(result.left, 140);
    EXPECT_EQ(result.top, 100);
}

TEST(RoiStabilizationTest, DisabledModePassesThroughRaw) {
    stabilization_config config{};
    config.enabled = false;
    config.mode = stabilization_mode::hybrid;

    stabilized_state state{};
    const sf::rectangle raw_bounds{50, 60, 30, 40};
    const auto result = stabilize_bounds(raw_bounds, state, config, 5.0f, -2.0f);

    EXPECT_EQ(result, raw_bounds);
}
