// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "config_parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <gst/gst.h>
#include <optional>

#include "../../libvcai/include/logging.h"
#include "string_utils.h"

/**
 * @file config_parser.cpp
 * @brief Implementation of configuration file parsing utilities.
 */

/// GStreamer debug category for configuration parsing utilities.
GST_DEBUG_CATEGORY_STATIC(config_parser);

void init_config_parser_debug_category() {
    GST_DEBUG_CATEGORY_INIT(config_parser, "config-parser", 0, "Configuration file parsing utilities");
}

namespace {
    /**
     * @brief Retrieves the label file path from the GstNvInfer element's configuration file.
     * @param gie A pointer to the GstNvInfer element.
     * @return The path to the label file if found, otherwise std::nullopt.
     */
    std::optional<std::filesystem::path> get_label_file_path(GstElement *gie) {
        gchar *config_path_str = nullptr;
        g_object_get(G_OBJECT(gie), "config-file-path", &config_path_str, nullptr);

        if (!config_path_str) {
            sf::warning(config_parser, "Failed to retrieve config-file-path from GstNvInfer element.");
            return std::nullopt;
        }

        std::filesystem::path config_path(config_path_str);
        g_free(config_path_str);

        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            sf::warning(config_parser, "Could not open nvinfer config file: %s", config_path.c_str());
            return std::nullopt;
        }

        std::string line;
        std::string label_path;
        std::string key = "labelfile-path";
        while (std::getline(config_file, line)) {
            auto it = std::ranges::search(line, key, [](const char ch1, const char ch2) {
                          return std::tolower(ch1) == std::tolower(ch2);
                      }).begin();
            if (it != line.end()) {
                // Look for both colon and equals sign separators
                size_t separator_pos = line.find('=');
                if (separator_pos == std::string::npos) {
                    separator_pos = line.find(':');
                }
                if (separator_pos != std::string::npos) {
                    label_path = line.substr(separator_pos + 1);
                    label_path = sf::trim(label_path);
                    break;
                }
            }
        }

        if (label_path.empty()) {
            sf::warning(config_parser, "Could not find 'labelfile-path' in %s", config_path.c_str());
            return std::nullopt;
        }

        std::filesystem::path result(label_path);
        if (result.is_relative()) {
            result = config_path.parent_path() / result;
        }

        return result;
    }

    /**
     * @brief Reads the label file and returns a vector of strings.
     * @param label_file_path The path to the label file.
     * @return A vector of strings, where each string is a label.
     */
    std::vector<std::string> read_label_file(const std::filesystem::path &label_file_path) {
        std::vector<std::string> labels;
        std::ifstream label_file(label_file_path);
        if (!label_file) {
            return labels;
        }

        std::string line;
        while (std::getline(label_file, line)) {
            labels.push_back(sf::trim(line));
        }

        return labels;
    }
} // namespace

/**
 * @brief Gets the class IDs for the given labels.
 * @param gie A pointer to the GstNvInfer element.
 * @param labels A vector of strings, where each string is a label.
 * @return A vector of class IDs.
 */
std::vector<size_t> get_class_ids(GstElement *gie, const std::vector<std::string> &labels) {
    const auto label_file_path = get_label_file_path(gie);
    if (!label_file_path.has_value()) {
        sf::warning(config_parser, "Could not find label file path in the config file.");
        return {};
    }

    const auto all_labels = read_label_file(label_file_path.value());
    if (all_labels.empty()) {
        sf::warning(config_parser, "Could not read labels from the label file.");
        return {};
    }

    if (labels.empty()) {
        std::vector<size_t> all_class_ids;
        for (size_t i = 0; i < all_labels.size(); ++i) {
            all_class_ids.push_back(i);
        }
        return all_class_ids;
    }

    std::vector<size_t> class_ids;
    for (size_t i = 0; i < all_labels.size(); ++i) {
        for (const auto &label: labels) {
            if (all_labels[i] == label) {
                class_ids.push_back(i);
            }
        }
    }
    return class_ids;
}
