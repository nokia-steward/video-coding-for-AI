// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

// Tests for UDP input configuration validation when input_type is set via CLI.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../common/integration_test_helpers.h"

namespace {
    [[nodiscard]] bool is_missing_gpu_plugin_error(const std::string &message) {
        return message.find("Encoder element plugin not found") != std::string::npos ||
               message.find("GPU elements unavailable") != std::string::npos ||
               message.find("Failed to create GPU encoder") != std::string::npos;
    }

    [[nodiscard]] command_line_parameters build_udp_cli_params(const std::filesystem::path &config_path) {
        const std::string config_arg = "--config";
        const std::string input_type_arg = "--common-input-type";
        const std::string input_type_value = "udp";
        const std::string config_value = config_path.string();

        const char *argv[] = {"test_encoder",         config_arg.c_str(),       config_value.c_str(),
                              input_type_arg.c_str(), input_type_value.c_str(), nullptr};
        const int argc = 5;

        return parse_command_line(argc, const_cast<char **>(argv));
    }

    [[nodiscard]] std::filesystem::path get_config_path(const std::string &filename) {
        const auto data_dir = get_test_data_dir();
        return data_dir / "configs" / filename;
    }
} // namespace

TEST(UdpConfigValidation, MissingUdpSectionWhenInputTypeSetViaCli) {
    const auto config_path = get_config_path("test_udp_missing_section.ini");
    std::ofstream config_file(config_path);
    ASSERT_TRUE(config_file.is_open());

    config_file << "[Common]\n";
    config_file << "input_type=file\n";
    config_file << "width=32\n";
    config_file << "height=32\n";
    config_file << "framerate=5\n";
    config_file.flush();
    config_file.close();

    const auto params = build_udp_cli_params(config_path);

    try {
        (void) load_and_configure_encoder(params);
        GTEST_FAIL() << "Expected configuration validation to fail for missing UDP section.";
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("UDP configuration is required"), std::string::npos);
        EXPECT_NE(message.find("UDP.port"), std::string::npos);
        EXPECT_NE(message.find("UDP.encoding"), std::string::npos);
        EXPECT_NE(message.find("UDP.payload_type"), std::string::npos);
        EXPECT_NE(message.find("UDP.clock_rate"), std::string::npos);
        EXPECT_NE(message.find("UDP.latency_ms"), std::string::npos);
    }
}

TEST(UdpConfigValidation, MissingUdpKeysWhenInputTypeSetViaCli) {
    const auto config_path = get_config_path("test_udp_missing_keys.ini");
    std::ofstream config_file(config_path);
    ASSERT_TRUE(config_file.is_open());

    config_file << "[Common]\n";
    config_file << "input_type=file\n";
    config_file << "width=32\n";
    config_file << "height=32\n";
    config_file << "framerate=5\n";

    config_file << "\n[UDP]\n";
    config_file << "port=5004\n";
    config_file.flush();
    config_file.close();

    const auto params = build_udp_cli_params(config_path);

    try {
        (void) load_and_configure_encoder(params);
        GTEST_FAIL() << "Expected configuration validation to fail for missing UDP keys.";
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("UDP.encoding"), std::string::npos);
        EXPECT_NE(message.find("UDP.payload_type"), std::string::npos);
        EXPECT_NE(message.find("UDP.clock_rate"), std::string::npos);
        EXPECT_NE(message.find("UDP.latency_ms"), std::string::npos);
    }
}

TEST(UdpConfigValidation, AllowsCompleteUdpConfigWhenInputTypeSetViaCli) {
    const auto config_path = get_config_path("test_udp_complete_config.ini");
    std::ofstream config_file(config_path);
    ASSERT_TRUE(config_file.is_open());

    config_file << "[Common]\n";
    config_file << "input_type=file\n";
    config_file << "width=32\n";
    config_file << "height=32\n";
    config_file << "framerate=5\n";

    config_file << "\n[UDP]\n";
    config_file << "bind_address=0.0.0.0\n";
    config_file << "port=5004\n";
    config_file << "encoding=H265\n";
    config_file << "payload_type=96\n";
    config_file << "clock_rate=90000\n";
    config_file << "latency_ms=200\n";
    config_file.flush();
    config_file.close();

    const auto params = build_udp_cli_params(config_path);

    try {
        (void) load_and_configure_encoder(params);
    } catch (const std::runtime_error &error) {
        if (is_missing_gpu_plugin_error(error.what())) {
            GTEST_SKIP() << error.what();
        }
        throw;
    }
}
