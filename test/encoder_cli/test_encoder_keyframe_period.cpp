// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <string>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/roi/sfu_config.h"

namespace {
    // Builds a config with the given intra period set via ROI metadata (mirrors
    // how the input encoding pipeline derives its GOP), packer type, and encoder element.
    encoder_config make_config(const int intra_period, const packer_type packer, const std::string &encoder_element) {
        encoder_config config;
        config.packer_type_value = packer;
        config.encoder_element = encoder_element.empty() ? std::nullopt : std::optional<std::string>(encoder_element);
        roi_sequence_info info{};
        info.frame_rate_details = frame_rate_info{intra_period, 30, intra_period, 0};
        config.roi_info = info;
        return config;
    }
} // namespace

TEST(EncoderKeyframePeriodTest, RandomAccessUsesConfiguredIntraPeriodForNonKvazaarEncoder) {
    // nvh265enc uses gop-size, so the configured intra period is passed through unaligned.
    const auto config = make_config(32, packer_type::random_access, "nvh265enc");
    EXPECT_EQ(get_encoder_keyframe_period(config), 32);
}

TEST(EncoderKeyframePeriodTest, FallbackDefaultIsUsedWhenIntraPeriodUnset) {
    // No ROI metadata -> get_intra_period falls back to 32; random access passthrough.
    encoder_config config;
    config.packer_type_value = packer_type::random_access;
    config.encoder_element = "nvh265enc";
    EXPECT_EQ(get_intra_period(config), 32);
    EXPECT_EQ(get_encoder_keyframe_period(config), 32);
}

TEST(EncoderKeyframePeriodTest, KvazaarRoundsIntraPeriodUpToBGopBlock) {
    // kvazaarenc must use a multiple of the B-GOP block (16); 30 -> 32.
    const auto config = make_config(30, packer_type::random_access, "kvazaarenc");
    EXPECT_EQ(get_encoder_keyframe_period(config), 32);
}

TEST(EncoderKeyframePeriodTest, KvazaarPassesAlreadyAlignedIntraPeriod) {
    const auto config = make_config(48, packer_type::random_access, "kvazaarenc");
    EXPECT_EQ(get_encoder_keyframe_period(config), 48);
}

TEST(EncoderKeyframePeriodTest, LowDelayUsesFixedGopForNonKvazaarEncoder) {
    // Low-delay packer uses a fixed GOP of 4, not the random-access intra period.
    const auto config = make_config(32, packer_type::low_delay, "nvh265enc");
    EXPECT_EQ(get_encoder_keyframe_period(config), 4);
}

TEST(EncoderKeyframePeriodTest, LowDelayKvazaarRoundsFixedGopUpToBGopBlock) {
    // kvazaarenc requires a multiple of the B-GOP block (16), so even the low-delay GOP of 4
    // is rounded up to 16.
    const auto config = make_config(32, packer_type::low_delay, "kvazaarenc");
    EXPECT_EQ(get_encoder_keyframe_period(config), 16);
}

TEST(EncoderKeyframePeriodTest, KvazaarAliasRecognized) {
    const auto config = make_config(30, packer_type::random_access, "kvazaar");
    EXPECT_TRUE(encoder_uses_kvazaar_intra_period(config));
    EXPECT_EQ(get_encoder_keyframe_period(config), 32);
}
