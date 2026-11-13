// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/cli_params.h"
#include "../common/gst_test_fixture.h"

#include <string>
#include <vector>

namespace {
    class CliParamsParsingTest : public GStreamerTestFixture {
    protected:
        [[nodiscard]] command_line_parameters parse_args(const std::vector<std::string> &args) const {
            std::vector<std::string> values;
            values.reserve(args.size() + 3);
            values.emplace_back("test_encoder");
            values.emplace_back("--config");
            values.emplace_back("/tmp/test_encoder_config.ini");
            values.insert(values.end(), args.begin(), args.end());

            std::vector<char *> argv;
            argv.reserve(values.size() + 1);
            for (auto &value: values) {
                argv.push_back(value.data());
            }
            argv.push_back(nullptr);
            const int argc = static_cast<int>(values.size());
            return parse_command_line(argc, argv.data());
        }
    };

} // namespace

TEST_F(CliParamsParsingTest, RoiPackRoisAcceptsExplicitBooleanValues) {
    const auto params_true = parse_args({"--roi-pack-rois", "true"});
    ASSERT_TRUE(params_true.roi.pack_rois.has_value());
    EXPECT_TRUE(*params_true.roi.pack_rois);

    const auto params_false = parse_args({"--roi-pack-rois", "false"});
    ASSERT_TRUE(params_false.roi.pack_rois.has_value());
    EXPECT_FALSE(*params_false.roi.pack_rois);

    const auto params_yes = parse_args({"--roi-pack-rois", "yes"});
    ASSERT_TRUE(params_yes.roi.pack_rois.has_value());
    EXPECT_TRUE(*params_yes.roi.pack_rois);

    const auto params_zero = parse_args({"--roi-pack-rois", "0"});
    ASSERT_TRUE(params_zero.roi.pack_rois.has_value());
    EXPECT_FALSE(*params_zero.roi.pack_rois);
}

TEST_F(CliParamsParsingTest, EncoderQpAcceptsBoundaryValues) {
    const auto params_zero = parse_args({"--encoder-qp", "0"});
    ASSERT_TRUE(params_zero.encoder.qp.has_value());
    EXPECT_EQ(*params_zero.encoder.qp, 0);

    const auto params_max = parse_args({"--encoder-qp", "51"});
    ASSERT_TRUE(params_max.encoder.qp.has_value());
    EXPECT_EQ(*params_max.encoder.qp, 51);
}

TEST_F(CliParamsParsingTest, RoiBackgroundModeParsesKnownValues) {
    const auto params_off = parse_args({"--roi-background-mode", "off"});
    ASSERT_TRUE(params_off.roi.background_mode_value.has_value());
    EXPECT_EQ(*params_off.roi.background_mode_value, background_mode::off);

    const auto params_blur = parse_args({"--roi-background-mode", "background_blur"});
    ASSERT_TRUE(params_blur.roi.background_mode_value.has_value());
    EXPECT_EQ(*params_blur.roi.background_mode_value, background_mode::background_blur);

    const auto params_qp = parse_args({"--roi-background-mode", "qp_map_delta"});
    ASSERT_TRUE(params_qp.roi.background_mode_value.has_value());
    EXPECT_EQ(*params_qp.roi.background_mode_value, background_mode::qp_map_delta);
}

TEST_F(CliParamsParsingTest, LumaAdaptationCliValuesAreParsed) {
    const auto params = parse_args({"--enable-luma-adaptation", "true", "--luma-downscale-factor", "0.72"});
    ASSERT_TRUE(params.roi.enable_luma_adaptation.has_value());
    EXPECT_TRUE(*params.roi.enable_luma_adaptation);
    ASSERT_TRUE(params.roi.luma_downscale_factor.has_value());
    EXPECT_DOUBLE_EQ(*params.roi.luma_downscale_factor, 0.72);
}

TEST_F(CliParamsParsingTest, DebugAllEnablesExpectedFlags) {
    const auto params = parse_args({"--debug-all"});

    EXPECT_TRUE(params.debug.debug_all);
    ASSERT_TRUE(params.debug.draw_borders.has_value());
    EXPECT_TRUE(*params.debug.draw_borders);
    ASSERT_TRUE(params.debug.draw_rtp_timestamp.has_value());
    EXPECT_TRUE(*params.debug.draw_rtp_timestamp);
    ASSERT_TRUE(params.debug.draw_fps.has_value());
    EXPECT_TRUE(*params.debug.draw_fps);
    ASSERT_TRUE(params.debug.draw_bitrate.has_value());
    EXPECT_TRUE(*params.debug.draw_bitrate);
    ASSERT_TRUE(params.debug.save_frames.has_value());
    EXPECT_TRUE(*params.debug.save_frames);
    ASSERT_TRUE(params.debug.enable_display_branch.has_value());
    EXPECT_TRUE(*params.debug.enable_display_branch);
    ASSERT_TRUE(params.debug.enable_mp4_branch.has_value());
    EXPECT_TRUE(*params.debug.enable_mp4_branch);
    ASSERT_TRUE(params.debug.enable_h265_branch.has_value());
    EXPECT_TRUE(*params.debug.enable_h265_branch);
    ASSERT_TRUE(params.logging.packer_logging.has_value());
    EXPECT_TRUE(*params.logging.packer_logging);
}

TEST_F(CliParamsParsingTest, CommunicationTransportParsesKnownValues) {
    // CLI accepts "rtp" (mapped to transport_type::udp) and "webrtc" only.
    const auto params_udp = parse_args({"--communication-transport-type", "rtp"});
    ASSERT_TRUE(params_udp.communication.transport_type.has_value());
    EXPECT_EQ(*params_udp.communication.transport_type, sf::communication::transport_type::udp);

    const auto params_webrtc = parse_args({"--communication-transport-type", "webrtc"});
    ASSERT_TRUE(params_webrtc.communication.transport_type.has_value());
    EXPECT_EQ(*params_webrtc.communication.transport_type, sf::communication::transport_type::webrtc);
}

TEST_F(CliParamsParsingTest, CommunicationPortParsesUpperBoundary) {
    const auto params = parse_args({"--communication-port", "65535"});
    ASSERT_TRUE(params.communication.port.has_value());
    EXPECT_EQ(*params.communication.port, 65535);
}
