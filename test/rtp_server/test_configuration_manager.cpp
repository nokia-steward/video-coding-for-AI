// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "../../rtp_server/src/core/configuration_manager.h"
#include "../../rtp_server/src/core/rtp_server_config.h"

namespace {
    // Test fixture for configuration manager tests
    class ConfigurationManagerTest : public ::testing::Test {
    protected:
        const std::filesystem::path temp_dir = "temp_config_manager_test";
        const std::filesystem::path config_file = temp_dir / "test_config.ini";

        void SetUp() override { std::filesystem::create_directory(temp_dir); }

        void TearDown() override { std::filesystem::remove_all(temp_dir); }

        void create_config_file(const std::string &content) {
            std::ofstream file(config_file);
            file << content;
        }

        // Helper to create a basic valid config
        void create_basic_config() {
            create_config_file(R"(
[Common]
background_color=0

[Server]
handshake_port=5004

[Monitoring]
enable_packet_loss_check=false
enable_jitter_check=false
enable_silence_check=false
max_packet_loss_rate=5.0
max_jitter_ms=50.0
max_silence_ms=2000

[RTPOutput]
enabled=false
host=127.0.0.1
port=5000

[WebRTC]
signaling_mode=embedded
signaling_port=8080
signaling_use_ssl=false
signaling_url=ws://localhost:8080
stun_server=stun:stun.l.google.com:19302
turn_server=
turn_username=
turn_password=
ice_timeout_ms=10000

[Debug]
save_frames=false

[TET]
enabled=false
tile_file=
qp=27
dynamic_qp=false
)");
        }
    };

    TEST_F(ConfigurationManagerTest, LoadConfigNoOverrides) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        auto config = load_and_configure_rtp_server(params);

        EXPECT_EQ(config.handshake_port, 5004);
        EXPECT_EQ(config.background_color, sf::background_color::green); // 0 maps to green
        EXPECT_FALSE(config.monitoring_config.enable_packet_loss_check);
        EXPECT_EQ(config.config_directory, std::filesystem::absolute(config_file).parent_path());
    }

    TEST_F(ConfigurationManagerTest, OverrideCommonParameters) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.handshake_port = 6000;

        // Cast integer 1 to background_color as black is not a named enum member
        params.background_color = static_cast<sf::background_color>(1);
        params.video_width = 2560;
        params.video_height = 1600;

        auto config = load_and_configure_rtp_server(params);

        EXPECT_EQ(config.handshake_port, 6000);
        EXPECT_EQ(config.background_color, static_cast<sf::background_color>(1));
        EXPECT_EQ(config.video_width, 2560);
        EXPECT_EQ(config.video_height, 1600);
    }

    TEST_F(ConfigurationManagerTest, OverrideMonitoringParameters) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.enable_packet_loss_check = true;

        params.max_packet_loss_rate = 10.5;

        auto config = load_and_configure_rtp_server(params);

        EXPECT_TRUE(config.monitoring_config.enable_packet_loss_check);
        EXPECT_DOUBLE_EQ(config.monitoring_config.max_packet_loss_rate, 10.5);
    }

    TEST_F(ConfigurationManagerTest, OverrideRtpOutputParameters) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.enable_rtp_output = true;

        params.rtp_output_host = "192.168.1.100";

        params.rtp_output_port = 5555;

        auto config = load_and_configure_rtp_server(params);

        EXPECT_TRUE(config.rtp_output.enabled);
        EXPECT_EQ(config.rtp_output.host, "192.168.1.100");
        EXPECT_EQ(config.rtp_output.port, 5555);
    }

    TEST_F(ConfigurationManagerTest, OverrideOutputDirectory) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.output_directory = "/tmp/override";

        auto config = load_and_configure_rtp_server(params);

        ASSERT_TRUE(config.output_directory.has_value());
        EXPECT_EQ(*config.output_directory, "/tmp/override");
    }

    TEST_F(ConfigurationManagerTest, ClearOutputDirectory) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.output_directory = ""; // Empty string to clear

        auto config = load_and_configure_rtp_server(params);

        EXPECT_FALSE(config.output_directory.has_value());
    }

    TEST_F(ConfigurationManagerTest, OverrideTransportType) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.communication_transport_type = transport_type::webrtc;

        auto config = load_and_configure_rtp_server(params);

        EXPECT_EQ(config.communication_transport_type, transport_type::webrtc);
    }

    TEST_F(ConfigurationManagerTest, OverrideWebRTCParameters) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.webrtc_signaling_port = 9000;

        params.webrtc_signaling_use_ssl = true;

        auto config = load_and_configure_rtp_server(params);

        EXPECT_EQ(config.webrtc_signaling_port, 9000);
        EXPECT_TRUE(config.webrtc_signaling_use_ssl);
    }

    TEST_F(ConfigurationManagerTest, OverrideTetParameters) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();

        params.tet_enabled = true;
        params.tet_tile_file = "tiles/sample.tet";
        params.tet_qp = 32;
        params.tet_dynamic_qp = true;

        auto config = load_and_configure_rtp_server(params);

        EXPECT_TRUE(config.tet_enabled);
        EXPECT_EQ(config.tet_tile_file, "tiles/sample.tet");
        EXPECT_EQ(config.tet_qp, 32U);
        EXPECT_TRUE(config.tet_dynamic_qp);
    }

    TEST_F(ConfigurationManagerTest, OverrideDebugSaveFrames) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();
        params.save_frames = true;

        auto config = load_and_configure_rtp_server(params);

        EXPECT_TRUE(config.save_frames);
    }

    TEST_F(ConfigurationManagerTest, OverrideSemsegTestModeParameters) {
        create_basic_config();

        command_line_parameters params;
        params.config_file = config_file.string();
        params.semseg_groundtruth_file = "/tmp/semseg_groundtruth.json";
        params.semseg_output_md = "/tmp/semseg_summary.md";
        params.semseg_output_json = "/tmp/semseg_summary.json";

        auto config = load_and_configure_rtp_server(params);

        EXPECT_EQ(config.semseg_groundtruth_file, "/tmp/semseg_groundtruth.json");
        ASSERT_TRUE(config.semseg_output_md.has_value());
        EXPECT_EQ(config.semseg_output_md.value(), "/tmp/semseg_summary.md");
        ASSERT_TRUE(config.semseg_output_json.has_value());
        EXPECT_EQ(config.semseg_output_json.value(), "/tmp/semseg_summary.json");
    }

    TEST_F(ConfigurationManagerTest, CreateDefaultConfig) {
        // Ensure config file doesn't exist
        std::filesystem::remove(config_file);

        command_line_parameters params;
        params.config_file = config_file.string();

        // This should create the default config file
        auto config = load_and_configure_rtp_server(params);

        EXPECT_TRUE(std::filesystem::exists(config_file));

        // Verify default values
        EXPECT_EQ(config.handshake_port, 5000); // Default port
    }
} // namespace
