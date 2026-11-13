// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../common/gst_test_fixture.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {
    class ConfigurationManagerOverridesTest : public GStreamerTestFixture {
    protected:
        [[nodiscard]] std::filesystem::path get_temp_dir() const {
            const auto dir = std::filesystem::temp_directory_path() / "vcai_configuration_manager_tests";
            std::filesystem::create_directories(dir);
            return dir;
        }

        void write_minimal_file_config(const std::filesystem::path &path, const std::optional<std::string> &roi_block,
                                       const std::optional<std::string> &debug_block) const {
            std::ofstream file(path);
            ASSERT_TRUE(file.is_open());

            file << "[Common]\n";
            file << "input_type=file\n";
            file << "width=640\n";
            file << "height=480\n";
            file << "framerate=30\n";
            file << "\n[Video]\n";
            file << "file_name=dummy_640x480_30.yuv\n";
            file << "format=I420\n";

            if (roi_block.has_value()) {
                file << "\n[ROI]\n";
                file << *roi_block;
            }
            if (debug_block.has_value()) {
                file << "\n[Debug]\n";
                file << *debug_block;
            }

            file.flush();
            file.close();
        }

        [[nodiscard]] command_line_parameters parse_args(const std::vector<std::string> &args) const {
            std::vector<char *> argv;
            argv.reserve(args.size() + 1);
            for (const auto &arg: args) {
                argv.push_back(const_cast<char *>(arg.c_str()));
            }
            argv.push_back(nullptr);
            const int argc = static_cast<int>(args.size());
            return parse_command_line(argc, argv.data());
        }
    };
} // namespace

TEST_F(ConfigurationManagerOverridesTest, GroundtruthCliOverrideDisablesInferenceAndClearsStaticRoi) {
    const auto config_path = get_temp_dir() / "groundtruth_override.ini";
    write_minimal_file_config(config_path, "inference_engine=true\nstatic_roi_json_file=static_rois.json\n",
                              std::nullopt);

    const auto params = parse_args(
            {"test_encoder", "--config", config_path.string(), "--roi-groundtruth-file", "groundtruth.json"});
    const auto config = load_and_configure_encoder(params);

    EXPECT_EQ(config.groundtruth_roi_file, "groundtruth.json");
    EXPECT_FALSE(config.inference_engine);
    EXPECT_TRUE(config.static_roi_json_file.empty());
}

TEST_F(ConfigurationManagerOverridesTest, DebugAllCliOverrideEnablesExpectedFlags) {
    const auto config_path = get_temp_dir() / "debug_all_override.ini";
    write_minimal_file_config(config_path, std::nullopt, "draw_borders=false\nenable_display_branch=false\n");

    const auto params = parse_args({"test_encoder", "--config", config_path.string(), "--debug-all"});
    const auto config = load_and_configure_encoder(params);

    EXPECT_TRUE(config.debug.draw_borders);
    EXPECT_TRUE(config.debug.draw_rtp_timestamp);
    EXPECT_TRUE(config.debug.draw_fps);
    EXPECT_TRUE(config.debug.save_frames);
    EXPECT_TRUE(config.debug.enable_display_branch);
    EXPECT_TRUE(config.debug.enable_mp4_branch);
    EXPECT_TRUE(config.debug.enable_h265_branch);
    EXPECT_TRUE(config.packer_logging);
}

TEST_F(ConfigurationManagerOverridesTest, CommunicationCliOverridesConfigValues) {
    const auto config_path = get_temp_dir() / "communication_override.ini";
    write_minimal_file_config(config_path, std::nullopt, std::nullopt);

    {
        std::ofstream file(config_path, std::ios::app);
        ASSERT_TRUE(file.is_open());
        file << "\n[Communication]\n";
        file << "transport_type=rtp\n";
        file << "host=127.0.0.1\n";
        file << "port=5000\n";
        file.flush();
        file.close();
    }

    const auto params = parse_args({"test_encoder", "--config", config_path.string(), "--communication-transport-type",
                                    "webrtc", "--communication-host", "192.168.10.1", "--communication-port", "6000",
                                    "--webrtc-signaling-url", "ws://localhost:8080"});
    const auto config = load_and_configure_encoder(params);

    EXPECT_EQ(config.communication.resolved_transport_type(), sf::communication::transport_type::webrtc);
    EXPECT_EQ(config.communication.host, "192.168.10.1");
    EXPECT_EQ(config.communication.port, 6000);
    EXPECT_EQ(config.webrtc.signaling_url, "ws://localhost:8080");
}

TEST_F(ConfigurationManagerOverridesTest, RoiBackgroundModeOverrideDisablesPackRois) {
    const auto config_path = get_temp_dir() / "roi_background_mode_override.ini";
    write_minimal_file_config(config_path, "pack_rois=true\nbackground_mode=off\n", std::nullopt);

    const auto params = parse_args({"test_encoder", "--config", config_path.string(), "--roi-background-mode",
                                    "qp_map_delta", "--roi-qp-map-background-delta", "12"});
    const auto config = load_and_configure_encoder(params);

    EXPECT_EQ(config.background_mode_value, background_mode::qp_map_delta);
    EXPECT_EQ(config.effective_background_mode_value, background_mode::qp_map_delta);
    EXPECT_EQ(config.qp_map_background_delta, 12);
    EXPECT_FALSE(config.pack_rois);
}

TEST_F(ConfigurationManagerOverridesTest, LumaAdaptationCliOverridesRoiConfig) {
    const auto config_path = get_temp_dir() / "luma_override.ini";
    write_minimal_file_config(config_path,
                              "enable_luma_adaptation=false\n"
                              "luma_downscale_factor=0.95\n",
                              std::nullopt);

    const auto params = parse_args({"test_encoder", "--config", config_path.string(), "--enable-luma-adaptation",
                                    "true", "--luma-downscale-factor", "0.65"});
    const auto config = load_and_configure_encoder(params);

    EXPECT_TRUE(config.enable_luma_adaptation);
    EXPECT_DOUBLE_EQ(config.luma_downscale_factor, 0.65);
}
