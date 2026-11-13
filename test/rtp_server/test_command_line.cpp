// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "../../libvcai/include/logging.h"
#include "../../rtp_server/src/core/command_line.h"

// Helper class to manage argc/argv for tests
class CommandLineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize GLib/GStreamer if needed (mostly for GOption)
        gst_init(nullptr, nullptr);
    }

    command_line_parameters parse_args(const std::vector<std::string> &args) {
        int argc = static_cast<int>(args.size() + 1);
        std::vector<char *> argv;
        std::string prog_name = "test_prog";
        argv.push_back(const_cast<char *>(prog_name.c_str()));

        for (const auto &arg: args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);

        return parse_command_line(argc, argv.data());
    }
};

TEST_F(CommandLineTest, DefaultValues) {
    auto params = parse_args({});
    EXPECT_EQ(params.config_file, "rtp_server_config.ini");
    EXPECT_FALSE(params.handshake_port.has_value());
    EXPECT_FALSE(params.background_color.has_value());
    EXPECT_FALSE(params.enable_packet_loss_check.has_value());
    EXPECT_FALSE(params.save_frames.has_value());
}

TEST_F(CommandLineTest, ConfigFile) {
    auto params = parse_args({"-c", "test_config.ini"});
    EXPECT_EQ(params.config_file, "test_config.ini");

    params = parse_args({"--config", "another_config.ini"});
    EXPECT_EQ(params.config_file, "another_config.ini");
}

TEST_F(CommandLineTest, CommonParameters) {
    auto params = parse_args({"--server-handshake-port", "1234"});
    EXPECT_TRUE(params.handshake_port.has_value());
    EXPECT_EQ(params.handshake_port.value(), static_cast<guint16>(1234));

    // background-color takes an int, map it to enum if needed,
    // but the code says: static_cast<sf::background_color>(background_color_raw)
    // assuming 0 is green (based on code comments in command_line.height/cpp)
    params = parse_args({"--common-background-color", "0"});
    EXPECT_TRUE(params.background_color.has_value());
    EXPECT_EQ(params.background_color.value(), sf::background_color::green);

    params = parse_args({"--common-width", "2560", "--common-height", "1600"});
    EXPECT_TRUE(params.video_width.has_value());
    EXPECT_TRUE(params.video_height.has_value());
    EXPECT_EQ(params.video_width.value(), 2560);
    EXPECT_EQ(params.video_height.value(), 1600);
}

TEST_F(CommandLineTest, MonitoringOptions) {
    auto params = parse_args({"--monitoring-enable-packet-loss-check", "--monitoring-enable-jitter-check",
                              "--monitoring-enable-silence-check", "--monitoring-max-packet-loss-rate", "5.5",
                              "--monitoring-max-jitter-ms", "10.0", "--monitoring-max-silence-ms", "5000"});

    EXPECT_TRUE(params.enable_packet_loss_check.has_value());
    EXPECT_TRUE(params.enable_packet_loss_check.value());

    EXPECT_TRUE(params.enable_jitter_check.has_value());
    EXPECT_TRUE(params.enable_jitter_check.value());

    EXPECT_TRUE(params.enable_silence_check.has_value());
    EXPECT_TRUE(params.enable_silence_check.value());

    EXPECT_TRUE(params.max_packet_loss_rate.has_value());
    EXPECT_DOUBLE_EQ(params.max_packet_loss_rate.value(), 5.5);

    EXPECT_TRUE(params.max_jitter_ms.has_value());
    EXPECT_DOUBLE_EQ(params.max_jitter_ms.value(), 10.0);

    EXPECT_TRUE(params.max_silence_ms.has_value());
    EXPECT_EQ(params.max_silence_ms.value(), static_cast<uint64_t>(5000));
}

TEST_F(CommandLineTest, RtpOutputOptions) {
    auto params = parse_args({"--rtp-output-enable", "--rtp-output-host", "127.0.0.1", "--rtp-output-port", "5000"});

    EXPECT_TRUE(params.enable_rtp_output.has_value());
    EXPECT_TRUE(params.enable_rtp_output.value());

    EXPECT_TRUE(params.rtp_output_host.has_value());
    EXPECT_EQ(params.rtp_output_host.value(), "127.0.0.1");

    EXPECT_TRUE(params.rtp_output_port.has_value());
    EXPECT_EQ(params.rtp_output_port.value(), static_cast<guint16>(5000));
}

TEST_F(CommandLineTest, OutputDirectory) {
    auto params = parse_args({"--server-output-directory", "/tmp/test"});
    EXPECT_TRUE(params.output_directory.has_value());
    EXPECT_EQ(params.output_directory.value(), "/tmp/test");
}

TEST_F(CommandLineTest, TestModeOptions) {
    auto params = parse_args({"--test-mode-groundtruth-file", "/tmp/groundtruth.json", "--test-mode-iou-threshold",
                              "0.6", "--test-mode-output-md", "/tmp/test_mode_summary.md", "--test-mode-output-json",
                              "/tmp/test_mode_summary.json"});
    EXPECT_TRUE(params.groundtruth_file.has_value());
    EXPECT_EQ(params.groundtruth_file.value(), "/tmp/groundtruth.json");
    EXPECT_TRUE(params.test_mode_iou_threshold.has_value());
    EXPECT_DOUBLE_EQ(params.test_mode_iou_threshold.value(), 0.6);
    EXPECT_TRUE(params.test_mode_output_md.has_value());
    EXPECT_EQ(params.test_mode_output_md.value(), "/tmp/test_mode_summary.md");
    EXPECT_TRUE(params.test_mode_output_json.has_value());
    EXPECT_EQ(params.test_mode_output_json.value(), "/tmp/test_mode_summary.json");
}

TEST_F(CommandLineTest, SemsegTestModeOptions) {
    auto params = parse_args({"--test-mode-semseg-groundtruth-file", "/tmp/semseg_groundtruth.json",
                              "--test-mode-semseg-output-md", "/tmp/test_mode_semseg_summary.md",
                              "--test-mode-semseg-output-json", "/tmp/test_mode_semseg_summary.json"});
    EXPECT_TRUE(params.semseg_groundtruth_file.has_value());
    EXPECT_EQ(params.semseg_groundtruth_file.value(), "/tmp/semseg_groundtruth.json");
    EXPECT_TRUE(params.semseg_output_md.has_value());
    EXPECT_EQ(params.semseg_output_md.value(), "/tmp/test_mode_semseg_summary.md");
    EXPECT_TRUE(params.semseg_output_json.has_value());
    EXPECT_EQ(params.semseg_output_json.value(), "/tmp/test_mode_semseg_summary.json");
}

TEST_F(CommandLineTest, TransportType) {
    auto params = parse_args({"--server-transport-type", "rtp"});
    EXPECT_TRUE(params.communication_transport_type.has_value());
    EXPECT_EQ(params.communication_transport_type.value(), transport_type::rtp);

    params = parse_args({"--server-transport-type", "webrtc"});
    EXPECT_TRUE(params.communication_transport_type.has_value());
    EXPECT_EQ(params.communication_transport_type.value(), transport_type::webrtc);
}

TEST_F(CommandLineTest, WebRTCOptions) {
    auto params = parse_args({"--webrtc-signaling-port", "8080", "--webrtc-signaling-ssl", "--webrtc-signaling-url",
                              "ws://localhost:8080", "--webrtc-stun-server", "stun:stun.l.google.com:19302",
                              "--webrtc-turn-server", "turn:turn.example.com", "--webrtc-turn-username", "user",
                              "--webrtc-turn-password", "pass", "--webrtc-ice-timeout-ms", "5000"});

    EXPECT_TRUE(params.webrtc_signaling_port.has_value());
    EXPECT_EQ(params.webrtc_signaling_port.value(), static_cast<uint16_t>(8080));

    EXPECT_TRUE(params.webrtc_signaling_use_ssl.has_value());
    EXPECT_TRUE(params.webrtc_signaling_use_ssl.value());

    EXPECT_TRUE(params.webrtc_signaling_url.has_value());
    EXPECT_EQ(params.webrtc_signaling_url.value(), "ws://localhost:8080");

    EXPECT_TRUE(params.webrtc_stun_server.has_value());
    EXPECT_EQ(params.webrtc_stun_server.value(), "stun:stun.l.google.com:19302");

    EXPECT_TRUE(params.webrtc_turn_server.has_value());
    EXPECT_EQ(params.webrtc_turn_server.value(), "turn:turn.example.com");

    EXPECT_TRUE(params.webrtc_turn_username.has_value());
    EXPECT_EQ(params.webrtc_turn_username.value(), "user");

    EXPECT_TRUE(params.webrtc_turn_password.has_value());
    EXPECT_EQ(params.webrtc_turn_password.value(), "pass");

    EXPECT_TRUE(params.webrtc_ice_timeout_ms.has_value());
    EXPECT_EQ(params.webrtc_ice_timeout_ms.value(), static_cast<uint32_t>(5000));
}

TEST_F(CommandLineTest, DebugOptions) {
    auto params = parse_args({"--debug-draw-bitrate", "--debug-save-frames"});
    EXPECT_TRUE(params.draw_bitrate.has_value());
    EXPECT_TRUE(params.draw_bitrate.value());
    EXPECT_TRUE(params.save_frames.has_value());
    EXPECT_TRUE(params.save_frames.value());
}

TEST_F(CommandLineTest, TetOptions) {
    auto params = parse_args(
            {"--tet-enable=true", "--tet-tile-file", "tiles/sample.tet", "--tet-qp", "32", "--tet-dynamic-qp"});
    EXPECT_TRUE(params.tet_enabled.has_value());
    EXPECT_TRUE(params.tet_enabled.value());
    EXPECT_TRUE(params.tet_tile_file.has_value());
    EXPECT_EQ(params.tet_tile_file.value(), "tiles/sample.tet");
    EXPECT_TRUE(params.tet_qp.has_value());
    EXPECT_EQ(params.tet_qp.value(), 32U);
    EXPECT_TRUE(params.tet_dynamic_qp.has_value());
    EXPECT_TRUE(params.tet_dynamic_qp.value());
}

TEST_F(CommandLineTest, TetCanBeExplicitlyDisabledFromCli) {
    const auto params = parse_args({"--tet-enable=false"});
    EXPECT_TRUE(params.tet_enabled.has_value());
    EXPECT_FALSE(params.tet_enabled.value());
}

TEST_F(CommandLineTest, DecoderLumaBackscaleDefaultsToOn) {
    const auto params = parse_args({});
    EXPECT_FALSE(params.enable_luma_backscale.has_value());
}

TEST_F(CommandLineTest, DecoderLumaBackscaleCanBeEnabledFromCli) {
    const auto params = parse_args({"--decoder-enable-luma-backscale=true"});
    EXPECT_TRUE(params.enable_luma_backscale.has_value());
    EXPECT_TRUE(params.enable_luma_backscale.value());
}

TEST_F(CommandLineTest, DecoderLumaBackscaleCanBeDisabledFromCli) {
    const auto params = parse_args({"--decoder-enable-luma-backscale=false"});
    EXPECT_TRUE(params.enable_luma_backscale.has_value());
    EXPECT_FALSE(params.enable_luma_backscale.value());
}

TEST_F(CommandLineTest, InvalidDecoderLumaBackscaleValueExits) {
    EXPECT_EXIT(parse_args({"--decoder-enable-luma-backscale=maybe"}), ::testing::ExitedWithCode(1), "");
}

// Tests for invalid inputs or edge cases

TEST_F(CommandLineTest, PortZeroIsIgnored) {
    // 0 is treated as "not set"
    auto params = parse_args({"--server-handshake-port", "0"});
    EXPECT_FALSE(params.handshake_port.has_value());
}

TEST_F(CommandLineTest, InvalidPortHigh) {
    // 65536 is invalid
    EXPECT_EXIT(parse_args({"--server-handshake-port", "65536"}), ::testing::ExitedWithCode(1), "");
}

TEST_F(CommandLineTest, InvalidPacketLossRateHigh) {
    EXPECT_EXIT(parse_args({"--monitoring-max-packet-loss-rate", "100.1"}), ::testing::ExitedWithCode(1), "");
}

TEST_F(CommandLineTest, PacketLossRateNegativeIsIgnored) {
    // -1.0 is treated as "not set" (default/sentinel)
    auto params = parse_args({"--monitoring-max-packet-loss-rate", "-1.0"});
    EXPECT_FALSE(params.max_packet_loss_rate.has_value());
}

TEST_F(CommandLineTest, JitterNegativeIsIgnored) {
    // -0.1 is treated as "not set" (default/sentinel) if code checks >= 0
    auto params = parse_args({"--monitoring-max-jitter-ms", "-0.1"});
    EXPECT_FALSE(params.max_jitter_ms.has_value());
}

TEST_F(CommandLineTest, InvalidRtpOutputPort) {
    EXPECT_EXIT(parse_args({"--rtp-output-port", "70000"}), ::testing::ExitedWithCode(1), "");
}

TEST_F(CommandLineTest, InvalidTransportType) {
    EXPECT_EXIT(parse_args({"--server-transport-type", "invalid"}), ::testing::ExitedWithCode(1), "");
}

TEST_F(CommandLineTest, InvalidTetEnableValueExits) {
    EXPECT_EXIT(parse_args({"--tet-enable=maybe"}), ::testing::ExitedWithCode(1), "");
}
