// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"

namespace {
    [[nodiscard]] bool is_missing_gpu_plugin_error(const std::string &message) {
        return message.find("Encoder element plugin not found") != std::string::npos ||
               message.find("GPU elements unavailable") != std::string::npos ||
               message.find("Failed to create GPU encoder") != std::string::npos;
    }

    [[nodiscard]] std::filesystem::path get_temp_test_dir() {
        const auto base_dir = std::filesystem::temp_directory_path() / "vcai_pack_rois_cli_tests";
        std::filesystem::create_directories(base_dir);
        return base_dir;
    }

    [[nodiscard]] std::filesystem::path write_dummy_input_file(const std::string &filename) {
        const auto video_path = get_temp_test_dir() / filename;
        std::ofstream video_file(video_path, std::ios::binary);
        EXPECT_TRUE(video_file.is_open());
        video_file.put('\0');
        video_file.close();
        return video_path;
    }

    void write_test_config(const std::filesystem::path &config_path, const std::filesystem::path &video_path,
                           const bool pack_rois_from_config) {
        std::ofstream config_file(config_path);
        ASSERT_TRUE(config_file.is_open());

        config_file << "[Common]\n";
        config_file << "input_type=file\n";
        config_file << "width=640\n";
        config_file << "height=480\n";
        config_file << "framerate=30\n";

        config_file << "\n[Video]\n";
        config_file << "file_name=" << video_path.string() << "\n";
        config_file << "format=I420\n";

        config_file << "\n[ROI]\n";
        config_file << "pack_rois=" << (pack_rois_from_config ? "true" : "false") << "\n";
        config_file << "background_mode=off\n";

        config_file.flush();
        config_file.close();
    }

    [[nodiscard]] command_line_parameters build_cli_params(const std::filesystem::path &config_path,
                                                           const std::optional<std::string> &pack_rois_value) {
        std::vector<std::string> args = {"test_encoder", "--config", config_path.string()};
        if (pack_rois_value.has_value()) {
            args.emplace_back("--roi-pack-rois");
            args.emplace_back(*pack_rois_value);
        }

        std::vector<char *> argv;
        argv.reserve(args.size());
        for (auto &arg: args) {
            argv.push_back(arg.data());
        }
        return parse_command_line(static_cast<int>(argv.size()), argv.data());
    }
} // namespace

TEST(RoiPackRoisCli, ExplicitTrueEnablesPackingFromCommandLine) {
    const auto config_path = get_temp_test_dir() / "pack_rois_cli_true.ini";
    const auto video_path = write_dummy_input_file("dummy_pack_rois_true_640x480_30.yuv");
    write_test_config(config_path, video_path, false);

    const auto params = build_cli_params(config_path, std::string("true"));
    encoder_config config;
    try {
        config = load_and_configure_encoder(params);
    } catch (const std::runtime_error &error) {
        if (is_missing_gpu_plugin_error(error.what())) {
            GTEST_SKIP() << error.what();
        }
        throw;
    }

    EXPECT_TRUE(config.pack_rois);
    EXPECT_EQ(config.background_mode_value, background_mode::off);
}

TEST(RoiPackRoisCli, ExplicitFalseDisablesPackingFromCommandLine) {
    const auto config_path = get_temp_test_dir() / "pack_rois_cli_false.ini";
    const auto video_path = write_dummy_input_file("dummy_pack_rois_false_640x480_30.yuv");
    write_test_config(config_path, video_path, true);

    const auto params = build_cli_params(config_path, std::string("false"));
    encoder_config config;
    try {
        config = load_and_configure_encoder(params);
    } catch (const std::runtime_error &error) {
        if (is_missing_gpu_plugin_error(error.what())) {
            GTEST_SKIP() << error.what();
        }
        throw;
    }

    EXPECT_FALSE(config.pack_rois);
    EXPECT_EQ(config.background_mode_value, background_mode::off);
}
