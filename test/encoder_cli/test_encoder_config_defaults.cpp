// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "../../encoder_cli/src/encoder_config.h"

namespace {
    class EncoderConfigDefaultsTest : public ::testing::Test {
    protected:
        std::filesystem::path make_temp_dir(const std::string &suffix) const {
            const auto unique_name = "vcai_config_defaults_" + suffix + "_" +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            const auto path = std::filesystem::temp_directory_path() / unique_name;
            std::filesystem::create_directories(path);
            return path;
        }
    };
} // namespace

TEST_F(EncoderConfigDefaultsTest, CreateDefaultConfigFileWritesRepresentativeDefaults) {
    const auto temp_dir = make_temp_dir("writes");
    const auto config_path = temp_dir / "encoder.ini";

    ASSERT_NO_THROW(create_default_config_file(config_path));
    ASSERT_TRUE(std::filesystem::exists(config_path));

    GError *error = nullptr;
    GKeyFile *key_file = g_key_file_new();
    ASSERT_NE(key_file, nullptr);
    ASSERT_TRUE(g_key_file_load_from_file(key_file, config_path.c_str(), G_KEY_FILE_NONE, &error));
    ASSERT_EQ(error, nullptr);

    gchar *input_type = g_key_file_get_string(key_file, "Common", "input_type", nullptr);
    ASSERT_NE(input_type, nullptr);
    EXPECT_EQ(std::string(input_type), "file");
    g_free(input_type);

    gchar *packer_type = g_key_file_get_string(key_file, "ROI", "packer_type", nullptr);
    ASSERT_NE(packer_type, nullptr);
    EXPECT_EQ(std::string(packer_type), packer_type_to_string(packer_type::low_delay));
    g_free(packer_type);

    EXPECT_EQ(g_key_file_get_integer(key_file, "Video", "width", nullptr), 1920);
    EXPECT_EQ(g_key_file_get_integer(key_file, "Video", "height", nullptr), 1080);
    EXPECT_TRUE(g_key_file_get_boolean(key_file, "ROI", "pack_rois", nullptr));
    EXPECT_FALSE(g_key_file_get_boolean(key_file, "ROI", "enable_luma_adaptation", nullptr));
    EXPECT_DOUBLE_EQ(g_key_file_get_double(key_file, "ROI", "luma_downscale_factor", nullptr), 1.0);
    EXPECT_FALSE(g_key_file_get_boolean(key_file, "Debug", "save_frames", nullptr));

    g_key_file_unref(key_file);
    std::filesystem::remove_all(temp_dir);
}

TEST_F(EncoderConfigDefaultsTest, CreateDefaultConfigFileOverwritesExistingFileContent) {
    const auto temp_dir = make_temp_dir("overwrite");
    const auto config_path = temp_dir / "encoder.ini";

    {
        std::ofstream preexisting(config_path);
        ASSERT_TRUE(preexisting.is_open());
        preexisting << "[Common]\ninput_type=camera\n";
    }

    ASSERT_NO_THROW(create_default_config_file(config_path));

    GError *error = nullptr;
    GKeyFile *key_file = g_key_file_new();
    ASSERT_NE(key_file, nullptr);
    ASSERT_TRUE(g_key_file_load_from_file(key_file, config_path.c_str(), G_KEY_FILE_NONE, &error));
    ASSERT_EQ(error, nullptr);

    gchar *input_type = g_key_file_get_string(key_file, "Common", "input_type", nullptr);
    ASSERT_NE(input_type, nullptr);
    EXPECT_EQ(std::string(input_type), "file");
    g_free(input_type);

    gchar *device = g_key_file_get_string(key_file, "Camera", "device", nullptr);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(std::string(device), "/dev/video0");
    g_free(device);

    g_key_file_unref(key_file);
    std::filesystem::remove_all(temp_dir);
}

TEST_F(EncoderConfigDefaultsTest, CreateDefaultConfigFileThrowsWhenParentDirectoryMissing) {
    const auto temp_dir = make_temp_dir("throws");
    const auto missing_parent = temp_dir / "does_not_exist";
    const auto config_path = missing_parent / "encoder.ini";

    EXPECT_THROW(
            {
                try {
                    create_default_config_file(config_path);
                } catch (const std::runtime_error &error) {
                    EXPECT_NE(std::string(error.what()).find("Failed to write configuration file"), std::string::npos);
                    throw;
                }
            },
            std::runtime_error);

    std::filesystem::remove_all(temp_dir);
}
