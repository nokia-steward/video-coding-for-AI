// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/nvenc/qp_map_support.h"

TEST(QpMapSupport, RequestedQpMapFallsBackToBackgroundBlurWhenUnsupported) {
    encoder_config config;
    config.background_mode_value = background_mode::qp_map_delta;
    config.effective_background_mode_value = background_mode::qp_map_delta;

    resolve_effective_background_mode(config, nullptr);

    EXPECT_EQ(config.effective_background_mode_value, background_mode::background_blur);
    EXPECT_FALSE(config.background_mode_fallback_reason.empty());
    EXPECT_EQ(config.qp_map_ctb_size, 64);
}

TEST(QpMapSupport, OffModeRemainsOffWhenNoQpMapRequested) {
    encoder_config config;
    config.background_mode_value = background_mode::off;
    config.effective_background_mode_value = background_mode::off;

    resolve_effective_background_mode(config, nullptr);

    EXPECT_EQ(config.background_mode_value, background_mode::off);
    EXPECT_EQ(config.effective_background_mode_value, background_mode::off);
}
