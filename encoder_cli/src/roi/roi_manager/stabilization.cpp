// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "stabilization.h"

#include <algorithm>
#include <cmath>

/**
 * @file stabilization.cpp
 * @brief Implementation of ROI stabilization utilities.
 */

namespace {
    constexpr float MAX_ADAPTIVE_ALPHA = 0.8f;
    constexpr float VELOCITY_ALPHA_SCALE = 0.01f;

    [[nodiscard]] float compute_adaptive_alpha(const stabilization_config &config, const float velocity_x,
                                               const float velocity_y) {
        const float velocity_magnitude = std::sqrt(velocity_x * velocity_x + velocity_y * velocity_y);
        const float velocity_adjustment = velocity_magnitude * config.velocity_adaptive_factor * VELOCITY_ALPHA_SCALE;
        return std::clamp(config.alpha + velocity_adjustment, 0.0f, MAX_ADAPTIVE_ALPHA);
    }

    [[nodiscard]] float apply_threshold(const float candidate, const float current, const int min_delta) {
        const float delta = std::fabs(candidate - current);
        if (delta < static_cast<float>(min_delta)) {
            return current;
        }
        return candidate;
    }
} // namespace

sf::rectangle stabilize_bounds(const sf::rectangle &raw, stabilized_state &state, const stabilization_config &config,
                               const float velocity_x, const float velocity_y) {
    if (!config.enabled || config.mode == stabilization_mode::none) {
        state.smoothed_x = static_cast<float>(raw.left);
        state.smoothed_y = static_cast<float>(raw.top);
        state.smoothed_w = static_cast<float>(raw.width);
        state.smoothed_h = static_cast<float>(raw.height);
        state.initialized = true;
        return raw;
    }

    if (!state.initialized) {
        state.smoothed_x = static_cast<float>(raw.left);
        state.smoothed_y = static_cast<float>(raw.top);
        state.smoothed_w = static_cast<float>(raw.width);
        state.smoothed_h = static_cast<float>(raw.height);
        state.initialized = true;
        return raw;
    }

    const bool use_ema = config.mode == stabilization_mode::ema || config.mode == stabilization_mode::hybrid;
    const bool use_threshold =
            config.mode == stabilization_mode::threshold || config.mode == stabilization_mode::hybrid;
    const float position_alpha = use_ema ? compute_adaptive_alpha(config, velocity_x, velocity_y) : 1.0f;
    const float size_alpha = use_ema ? std::clamp(config.alpha_size, 0.0f, 1.0f) : 1.0f;

    float candidate_x = static_cast<float>(raw.left);
    float candidate_y = static_cast<float>(raw.top);
    float candidate_w = static_cast<float>(raw.width);
    float candidate_h = static_cast<float>(raw.height);

    if (use_ema) {
        candidate_x = state.smoothed_x + position_alpha * (candidate_x - state.smoothed_x);
        candidate_y = state.smoothed_y + position_alpha * (candidate_y - state.smoothed_y);
        candidate_w = state.smoothed_w + size_alpha * (candidate_w - state.smoothed_w);
        candidate_h = state.smoothed_h + size_alpha * (candidate_h - state.smoothed_h);
    }

    if (use_threshold) {
        candidate_x = apply_threshold(candidate_x, state.smoothed_x, config.min_position_delta);
        candidate_y = apply_threshold(candidate_y, state.smoothed_y, config.min_position_delta);
        candidate_w = apply_threshold(candidate_w, state.smoothed_w, config.min_size_delta);
        candidate_h = apply_threshold(candidate_h, state.smoothed_h, config.min_size_delta);
    }

    state.smoothed_x = candidate_x;
    state.smoothed_y = candidate_y;
    state.smoothed_w = candidate_w;
    state.smoothed_h = candidate_h;

    return sf::rectangle{
            static_cast<int>(std::lround(state.smoothed_x)), static_cast<int>(std::lround(state.smoothed_y)),
            static_cast<int>(std::lround(state.smoothed_w)), static_cast<int>(std::lround(state.smoothed_h))};
}
