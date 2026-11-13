// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_config.h"
#include "encoder_config_parser.h"

#include <gst/gst.h>

#include "gst_raii.h"

/**
 * @file encoder_config_loader.cpp
 * @brief Implementation of encoder configuration file loading.
 *
 * File-loading orchestration only. The load-bearing parsing concern (typed
 * readers, string parsers, parameter-group readers) lives in
 * encoder_config_parser.cpp.
 */

/**
 * @brief Loads encoder configuration from an INI file.
 * @param config_file_path Path to the configuration file.
 * @return Parsed encoder configuration.
 */
encoder_config load_encoder_config(const std::filesystem::path &config_file_path) {
    encoder_config config{};

    const sf::unique_g_key_file_ptr key_file(g_key_file_new());
    GError *error = nullptr;

    // Load the configuration file
    if (!g_key_file_load_from_file(key_file.get(), config_file_path.c_str(), G_KEY_FILE_NONE, &error)) {
        const std::string error_msg = error ? error->message : "Unknown error";
        const std::string file_path_str = config_file_path.string();
        g_clear_error(&error);

        // Provide more context about the failure
        std::string additional_info;
        if (std::filesystem::exists(config_file_path)) {
            try {
                const auto file_size = std::filesystem::file_size(config_file_path);
                additional_info = " (file exists, size: " + std::to_string(file_size) + " bytes)";
            } catch (...) {
                additional_info = " (file exists but cannot read size)";
            }
        } else {
            additional_info = " (file does not exist)";
        }

        throw std::runtime_error(std::string("Failed to load configuration file: ") + error_msg + additional_info);
    }

    // Apply all configuration sections through the parsing module
    parse_config_sections(key_file.get(), config, error);

    // Note: Validation is moved to after command line overrides are applied
    // This allows command line options to override config file values

    return config;
}
