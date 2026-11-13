// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../../encoder_cli/src/encoder_config.h"

namespace {
    [[nodiscard]] bool is_missing_gpu_plugin_error(const std::string &message) {
        return message.find("Encoder element plugin not found") != std::string::npos ||
               message.find("GPU elements unavailable") != std::string::npos ||
               message.find("Failed to create GPU encoder") != std::string::npos;
    }

    [[nodiscard]] std::filesystem::path get_config_path(const std::string &filename) {
        const auto config_dir = std::filesystem::temp_directory_path() / "vcai_encoder_config_tests";
        std::filesystem::create_directories(config_dir);
        return config_dir / filename;
    }

    void write_minimal_file_input_config(const std::filesystem::path &config_path,
                                         const std::optional<std::string> &output_directory) {
        std::ofstream config_file(config_path);
        ASSERT_TRUE(config_file.is_open());

        config_file << "[Common]\n";
        config_file << "input_type=file\n";
        config_file << "width=640\n";
        config_file << "height=480\n";
        config_file << "framerate=30\n";
        config_file << "\n[Video]\n";
        config_file << "file_name=dummy_640x480_30.yuv\n";
        config_file << "format=I420\n";

        if (output_directory.has_value()) {
            config_file << "\n[Output]\n";
            config_file << "output_directory=" << *output_directory << "\n";
        }

        config_file.flush();
        config_file.close();
    }

    [[nodiscard]] command_line_parameters build_cli_params(const std::filesystem::path &config_path,
                                                           const std::optional<std::string> &output_directory) {
        std::vector<std::string> args = {"test_encoder", "--config", config_path.string()};
        if (output_directory.has_value()) {
            args.emplace_back("--output-directory");
            args.emplace_back(*output_directory);
        }

        std::vector<char *> argv;
        argv.reserve(args.size());
        for (auto &arg: args) {
            argv.push_back(arg.data());
        }

        return parse_command_line(static_cast<int>(argv.size()), argv.data());
    }
} // namespace

TEST(OutputDirectoryConfig, DefaultsToConfigDirectoryWhenNotSet) {
    const auto config_path = get_config_path("test_output_directory_default.ini");
    write_minimal_file_input_config(config_path, std::nullopt);

    const auto params = build_cli_params(config_path, std::nullopt);
    encoder_config config;
    try {
        config = load_and_configure_encoder(params);
    } catch (const std::runtime_error &error) {
        if (is_missing_gpu_plugin_error(error.what())) {
            GTEST_SKIP() << error.what();
        }
        throw;
    }

    const auto expected_root = std::filesystem::absolute(config_path).parent_path();
    EXPECT_FALSE(config.output_directory.has_value());
    EXPECT_EQ(config.config_directory, expected_root);
    EXPECT_EQ(resolve_frame_output_root_directory(config), expected_root);
}

TEST(OutputDirectoryConfig, LoadsOutputDirectoryFromConfig) {
    const auto output_dir =
            (std::filesystem::temp_directory_path() / "vcai_encoder_config_tests" / "from_config").string();
    const auto config_path = get_config_path("test_output_directory_from_config.ini");
    write_minimal_file_input_config(config_path, output_dir);

    const auto params = build_cli_params(config_path, std::nullopt);
    encoder_config config;
    try {
        config = load_and_configure_encoder(params);
    } catch (const std::runtime_error &error) {
        if (is_missing_gpu_plugin_error(error.what())) {
            GTEST_SKIP() << error.what();
        }
        throw;
    }

    ASSERT_TRUE(config.output_directory.has_value());
    EXPECT_EQ(*config.output_directory, output_dir);
    EXPECT_EQ(resolve_frame_output_root_directory(config), std::filesystem::path(output_dir));
}

TEST(OutputDirectoryConfig, CommandLineOverrideTakesPrecedence) {
    const auto config_output_dir =
            (std::filesystem::temp_directory_path() / "vcai_encoder_config_tests" / "from_config").string();
    const auto cli_output_dir =
            (std::filesystem::temp_directory_path() / "vcai_encoder_config_tests" / "from_cli").string();
    const auto config_path = get_config_path("test_output_directory_cli_override.ini");
    write_minimal_file_input_config(config_path, config_output_dir);

    const auto params = build_cli_params(config_path, cli_output_dir);
    encoder_config config;
    try {
        config = load_and_configure_encoder(params);
    } catch (const std::runtime_error &error) {
        if (is_missing_gpu_plugin_error(error.what())) {
            GTEST_SKIP() << error.what();
        }
        throw;
    }

    ASSERT_TRUE(config.output_directory.has_value());
    EXPECT_EQ(*config.output_directory, cli_output_dir);
    EXPECT_EQ(resolve_frame_output_root_directory(config), std::filesystem::path(cli_output_dir));
}
