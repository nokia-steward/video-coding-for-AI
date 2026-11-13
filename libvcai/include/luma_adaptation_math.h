// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <optional>

namespace sf {
    struct luma_adaptation_signal {
        bool ratio_luma_sign_flag{false};
        uint8_t ratio_luma_value{0U};
        int ratio_percentage{100};
        uint8_t backscale_ratio_value{0U};
    };

    /**
     * @brief Derives ratio percentage from EOI sign/value pair.
     */
    [[nodiscard]] int derive_luma_ratio_percentage(bool ratio_luma_sign_flag, uint8_t ratio_luma_value);

    /**
     * @brief Derives EOI luma adaptation fields from encoder-side downscale factor.
     * Backscale is full (1/d). Use the overload with luma_upscale_multiplier for adaptive upscale.
     */
    [[nodiscard]] luma_adaptation_signal derive_luma_adaptation_signal(double luma_downscale_factor);

    /**
     * @brief Derives EOI luma adaptation fields from downscale factor and optional upscale multiplier.
     * When luma_upscale_multiplier is set, backscale_ratio_value encodes that multiplier so the decoder
     * applies it (multiplier = backscale_ratio_value / ratio_percentage). When absent, full backscale (1/d).
     */
    [[nodiscard]] luma_adaptation_signal derive_luma_adaptation_signal(double luma_downscale_factor,
                                                                       std::optional<double> luma_upscale_multiplier);

    /**
     * @brief Computes decoder backscale multiplier from EOI ratio and backscale values.
     */
    [[nodiscard]] std::optional<double> compute_luma_backscale_multiplier(int ratio_percentage,
                                                                          int backscale_ratio_value);
} // namespace sf
