#include "deepstream_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nvds_yml_parser.h>

#include "logging.h"

GST_DEBUG_CATEGORY_EXTERN(display_branch_category);

bool configure_nvinfer(GstElement *nvinfer, const std::string &config_file) {
    if (!nvinfer || config_file.empty()) {
        sf::error(display_branch_category, "nvinfer element or config file is null/empty");
        return false;
    }

    if (!std::filesystem::exists(config_file)) {
        sf::error(display_branch_category, "DeepStream config file does not exist: %s", config_file.c_str());
        return false;
    }

    std::filesystem::path config_path(config_file);
    if (!config_path.is_absolute()) {
        const char *rtp_server_dir = getenv("RTP_SERVER_DIR");
        if (rtp_server_dir && std::filesystem::exists(rtp_server_dir)) {
            config_path = std::filesystem::path(rtp_server_dir) / config_path;
        } else {
            config_path = std::filesystem::absolute(config_path);
        }
        if (std::filesystem::exists(config_path)) {
            config_path = std::filesystem::canonical(config_path);
        }
    } else if (std::filesystem::exists(config_path)) {
        config_path = std::filesystem::canonical(config_path);
    }
    const std::string absolute_config_file = config_path.string();

    sf::info(display_branch_category, "Configuring nvinfer with config file: %s (absolute: %s)", config_file.c_str(),
             absolute_config_file.c_str());
    sf::info(display_branch_category, "Calling nvds_parse_gie with config file: %s, group: 'primary-gie'",
             absolute_config_file.c_str());

    const auto parse_result = nvds_parse_gie(nvinfer, const_cast<char *>(absolute_config_file.c_str()), "primary-gie");
    gchar *config_path_after_parse = nullptr;
    g_object_get(G_OBJECT(nvinfer), "config-file-path", &config_path_after_parse, nullptr);

    if (parse_result != NVDS_YAML_PARSER_SUCCESS) {
        if (config_path_after_parse) {
            sf::warning(display_branch_category,
                        "nvds_parse_gie returned error %d but config-file-path was set to: %s. "
                        "Continuing with extracted path.",
                        static_cast<int>(parse_result), config_path_after_parse);
            if (std::filesystem::exists(config_path_after_parse)) {
                g_free(config_path_after_parse);
            } else {
                sf::error(display_branch_category, "Extracted config-file-path does not exist: %s",
                          config_path_after_parse);
                g_free(config_path_after_parse);
                return false;
            }
        } else {
            sf::error(display_branch_category,
                      "nvds_parse_gie failed for config file: %s (result: %d). "
                      "Please verify a valid 'primary-gie:' section exists.",
                      absolute_config_file.c_str(), static_cast<int>(parse_result));
            std::ifstream file_check(absolute_config_file);
            if (file_check.good()) {
                std::string line;
                sf::info(display_branch_category, "Config file contents (first 20 lines) for troubleshooting:");
                int line_count = 0;
                while (std::getline(file_check, line) && line_count < 20) {
                    sf::info(display_branch_category, "  Line %d: %s", line_count + 1, line.c_str());
                    line_count++;
                }
            } else {
                sf::error(display_branch_category, "Could not open config file for reading: %s",
                          absolute_config_file.c_str());
            }
            return false;
        }
    } else if (config_path_after_parse) {
        g_free(config_path_after_parse);
        sf::info(display_branch_category, "nvds_parse_gie succeeded for config file: %s", absolute_config_file.c_str());
    }

    gchar *config_path_check = nullptr;
    g_object_get(G_OBJECT(nvinfer), "config-file-path", &config_path_check, nullptr);
    if (config_path_check) {
        const std::string extracted_path(config_path_check);
        sf::info(display_branch_category, "nvinfer config-file-path after parsing: %s", extracted_path.c_str());

        if (extracted_path != absolute_config_file) {
            if (!std::filesystem::exists(extracted_path)) {
                sf::error(display_branch_category, "Extracted config-file-path does not exist: %s",
                          extracted_path.c_str());
                g_free(config_path_check);
                return false;
            }
        } else {
            sf::warning(display_branch_category, "Extracted config-file-path matches main config file (unexpected). "
                                                 "YAML structure may be incorrect.");
        }
        g_free(config_path_check);
    } else {
        sf::error(display_branch_category, "config-file-path not set by nvds_parse_gie. "
                                           "Check 'config-file-path' in the 'primary-gie' section.");
        return false;
    }

    sf::info(display_branch_category, "Successfully configured nvinfer element");
    return true;
}

std::string determine_tracker_config_path(const std::string &config_file) {
    if (!config_file.empty()) {
        return config_file;
    }

    const std::string deepstream_path = "/opt/nvidia/deepstream/deepstream";
    const std::string config_filename = "config_tracker_NvSORT.yml";
    std::string config_path = deepstream_path + "/samples/configs/deepstream-app/" + config_filename;

    if (std::filesystem::exists(config_path)) {
        sf::info(display_branch_category, "Using default tracker config: %s", config_path.c_str());
        return config_path;
    }

    sf::error(display_branch_category, "Could not find DeepStream tracker configuration file at %s",
              config_path.c_str());
    return "";
}

bool configure_nvtracker(GstElement *nvtracker, const std::string &config_file, const std::string &lib_file,
                         const int width, const int height) {
    if (!nvtracker || lib_file.empty()) {
        sf::error(display_branch_category, "nvtracker element or lib_file is null/empty");
        return false;
    }

    const std::string final_config_file = determine_tracker_config_path(config_file);
    if (final_config_file.empty()) {
        sf::error(display_branch_category, "Could not determine tracker config file path");
        return false;
    }

    if (!std::filesystem::exists(final_config_file)) {
        sf::error(display_branch_category, "Tracker config file does not exist: %s", final_config_file.c_str());
        return false;
    }

    if (!std::filesystem::exists(lib_file)) {
        sf::error(display_branch_category, "Tracker library file does not exist: %s", lib_file.c_str());
        return false;
    }

    constexpr guint user_meta_pool_size = 256;
    g_object_set(G_OBJECT(nvtracker), "ll-lib-file", lib_file.c_str(), "ll-config-file", final_config_file.c_str(),
                 "tracker-width", width, "tracker-height", height, "gpu-id", 0, "display-tracking-id", TRUE,
                 "user-meta-pool-size", user_meta_pool_size, nullptr);

    sf::info(display_branch_category, "Configured nvtracker: config=%s, lib=%s, resolution=%dx%d",
             final_config_file.c_str(), lib_file.c_str(), width, height);
    return true;
}
