// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "gst_raii.h"

/**
 * @brief Per-frame luma-adaptation state used when deciding whether to signal EOI.
 * When upscale_multiplier is set, EOI signals that value for decoder back-scaling;
 * when absent, full backscale (1/downscale_factor) is signaled.
 */
struct encoded_frame_luma_adaptation_state {
    bool enabled = false;
    double downscale_factor = 1.0;
    std::optional<double> upscale_multiplier{};
};

/**
 * @brief Couples an encoded sample with its effective per-frame EOI state.
 */
struct encoded_frame_item {
    sf::unique_gst_sample_ptr sample{};
    uint64_t contract_sequence_id = 0;
    encoded_frame_luma_adaptation_state luma_adaptation{};
};

/**
 * @brief Contract-linked encoded callback state produced at encoder submission.
 */
struct pending_encoded_contract_state {
    uint64_t sequence_id = 0;
    encoded_frame_luma_adaptation_state luma_adaptation{};
};

/**
 * @brief Builds queue metadata from config-driven (fixed) luma-adaptation settings.
 */
[[nodiscard]] inline encoded_frame_luma_adaptation_state
make_configured_luma_adaptation_state(const bool enabled, const double downscale_factor) {
    if (!enabled) {
        return {};
    }

    return {.enabled = true, .downscale_factor = downscale_factor, .upscale_multiplier = std::nullopt};
}

/**
 * @brief Builds queue metadata for adaptive luma (analyzer-provided d and u).
 */
[[nodiscard]] inline encoded_frame_luma_adaptation_state
make_adaptive_luma_adaptation_state(const bool enabled, const double downscale_factor,
                                    const std::optional<double> upscale_multiplier) {
    if (!enabled) {
        return {};
    }

    return {.enabled = true, .downscale_factor = downscale_factor, .upscale_multiplier = upscale_multiplier};
}

/**
 * @brief Creates an encoded frame queue item.
 */
[[nodiscard]] inline encoded_frame_item
make_encoded_frame_item(sf::unique_gst_sample_ptr sample,
                        const encoded_frame_luma_adaptation_state luma_adaptation = {},
                        const uint64_t contract_sequence_id = 0) {
    encoded_frame_item item{};
    item.sample = std::move(sample);
    item.contract_sequence_id = contract_sequence_id;
    item.luma_adaptation = luma_adaptation;
    return item;
}
