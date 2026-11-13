// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "luma_adaptation_math.h"

#include <algorithm>
#include <cmath>

namespace sf {
    [[nodiscard]] int derive_luma_ratio_percentage(const bool ratio_luma_sign_flag, const uint8_t ratio_luma_value) {
        const int ratio_sign = ratio_luma_sign_flag ? -1 : 1;
        return 100 + ratio_sign * static_cast<int>(ratio_luma_value);
    }

    [[nodiscard]] luma_adaptation_signal derive_luma_adaptation_signal(const double luma_downscale_factor) {
        const int luma_percentage = std::clamp(static_cast<int>(std::lround(luma_downscale_factor * 100.0)), 0, 200);
        const bool ratio_luma_sign_flag = luma_percentage < 100;
        const uint8_t ratio_luma_value = static_cast<uint8_t>(std::clamp(std::abs(100 - luma_percentage), 0, 127));
        const int ratio_percentage = derive_luma_ratio_percentage(ratio_luma_sign_flag, ratio_luma_value);

        const double safe_downscale = std::max(luma_downscale_factor, 1e-6);
        const double desired_multiplier = 1.0 / safe_downscale;
        const int derived_backscale_ratio = static_cast<int>(std::lround(desired_multiplier * ratio_percentage));
        const uint8_t backscale_ratio_value = static_cast<uint8_t>(std::clamp(derived_backscale_ratio, 0, 127));

        return {.ratio_luma_sign_flag = ratio_luma_sign_flag,
                .ratio_luma_value = ratio_luma_value,
                .ratio_percentage = ratio_percentage,
                .backscale_ratio_value = backscale_ratio_value};
    }

    [[nodiscard]] luma_adaptation_signal
    derive_luma_adaptation_signal(const double luma_downscale_factor,
                                  const std::optional<double> luma_upscale_multiplier) {
        const luma_adaptation_signal base = derive_luma_adaptation_signal(luma_downscale_factor);
        if (!luma_upscale_multiplier.has_value()) {
            return base;
        }
        const double upscale_multiplier = *luma_upscale_multiplier;
        if (base.ratio_percentage <= 0 || upscale_multiplier <= 0.0) {
            return base;
        }
        const int derived_backscale =
                static_cast<int>(std::lround(upscale_multiplier * static_cast<double>(base.ratio_percentage)));
        const uint8_t backscale_ratio_value = static_cast<uint8_t>(std::clamp(derived_backscale, 0, 127));
        return {.ratio_luma_sign_flag = base.ratio_luma_sign_flag,
                .ratio_luma_value = base.ratio_luma_value,
                .ratio_percentage = base.ratio_percentage,
                .backscale_ratio_value = backscale_ratio_value};
    }

    [[nodiscard]] std::optional<double> compute_luma_backscale_multiplier(const int ratio_percentage,
                                                                          const int backscale_ratio_value) {
        if (ratio_percentage <= 0 || backscale_ratio_value <= 0) {
            return std::nullopt;
        }

        const double multiplier = static_cast<double>(backscale_ratio_value) / static_cast<double>(ratio_percentage);
        if (multiplier <= 0.0) {
            return std::nullopt;
        }
        return multiplier;
    }
} // namespace sf
