#include "label_utils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "logging.h"

GST_DEBUG_CATEGORY_EXTERN(display_branch_category);

std::string trim(const std::string &text) {
    const auto start = text.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = text.find_last_not_of(" \t\n\r");
    return text.substr(start, end - start + 1);
}

std::string normalize_label_name(const std::string &label) {
    static const std::unordered_map<std::string, std::string> aliases = {
            {"person", "pedestrian"},
            {"people", "pedestrian"},
            {"pedestrian", "pedestrian"},
            {"pedestrians", "pedestrian"},
            {"motorbike", "motorcycle"},
            {"motorbikes", "motorcycle"},
            {"motorcycle", "motorcycle"},
            {"motorcycles", "motorcycle"},
            {"bike", "bicycle"},
            {"bikes", "bicycle"},
            {"bicycle", "bicycle"},
            {"bicycles", "bicycle"},
            {"cyclist", "bicycle"},
            {"cyclists", "bicycle"},
            {"road", "road"},
            {"roads", "road"},
            {"sidewalk", "sidewalk"},
            {"sidewalks", "sidewalk"},
            {"pavement", "sidewalk"},
            {"pavements", "sidewalk"},
            {"vegetation", "vegetation"},
            {"tree", "vegetation"},
            {"trees", "vegetation"},
            {"car", "car"},
            {"cars", "car"},
            {"truck", "truck"},
            {"trucks", "truck"},
            {"bus", "bus"},
            {"buses", "bus"},
            {"building", "building"},
            {"buildings", "building"},
            {"background", "background"},
    };

    const std::string trimmed = trim(label);
    std::string compact;
    compact.reserve(trimmed.size());
    for (const char raw_character: trimmed) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (std::isalnum(character)) {
            compact.push_back(static_cast<char>(std::tolower(character)));
        }
    }

    if (compact.empty()) {
        return compact;
    }
    const auto alias_iter = aliases.find(compact);
    if (alias_iter != aliases.end()) {
        return alias_iter->second;
    }
    return compact;
}

std::vector<std::string> split_labels(const std::string &text) {
    std::vector<std::string> result;
    if (text.empty()) {
        return result;
    }
    std::istringstream stream(text);
    std::string label;
    while (std::getline(stream, label, ',')) {
        const std::string trimmed = trim(label);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

std::string get_label_file_path(GstElement *nvinfer) {
    gchar *config_path_str = nullptr;
    g_object_get(G_OBJECT(nvinfer), "config-file-path", &config_path_str, nullptr);

    if (!config_path_str) {
        sf::warning(display_branch_category, "Failed to retrieve config-file-path from nvinfer element");
        return "";
    }

    std::filesystem::path config_path(config_path_str);
    g_free(config_path_str);

    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        sf::warning(display_branch_category, "Could not open nvinfer config file: %s", config_path.c_str());
        return "";
    }

    std::string line;
    std::string label_path;
    const std::string key = "labelfile-path";
    while (std::getline(config_file, line)) {
        auto it = std::ranges::search(line, key, [](const char left, const char right) {
                      return std::tolower(left) == std::tolower(right);
                  }).begin();
        if (it != line.end()) {
            size_t separator_position = line.find('=');
            if (separator_position == std::string::npos) {
                separator_position = line.find(':');
            }
            if (separator_position != std::string::npos) {
                label_path = line.substr(separator_position + 1);
                label_path = trim(label_path);
                break;
            }
        }
    }

    if (label_path.empty()) {
        sf::warning(display_branch_category, "Could not find 'labelfile-path' in %s", config_path.c_str());
        return "";
    }

    std::filesystem::path result(label_path);
    if (result.is_relative()) {
        result = config_path.parent_path() / result;
    }

    return result.string();
}

std::vector<std::string> read_label_file(const std::filesystem::path &label_file_path) {
    std::vector<std::string> labels;
    std::ifstream label_file(label_file_path);
    if (!label_file) {
        return labels;
    }

    std::string line;
    while (std::getline(label_file, line)) {
        labels.push_back(trim(line));
    }

    return labels;
}

std::vector<size_t> get_class_ids(GstElement *nvinfer, const std::vector<std::string> &labels) {
    const std::string label_file_path_str = get_label_file_path(nvinfer);
    if (label_file_path_str.empty()) {
        sf::warning(display_branch_category, "Could not find label file path in the config file");
        return {};
    }

    const std::filesystem::path label_file_path(label_file_path_str);
    const auto all_labels = read_label_file(label_file_path);
    if (all_labels.empty()) {
        sf::warning(display_branch_category, "Could not read labels from the label file: %s",
                    label_file_path_str.c_str());
        return {};
    }

    if (labels.empty()) {
        std::vector<size_t> all_class_ids;
        for (size_t index = 0; index < all_labels.size(); ++index) {
            all_class_ids.push_back(index);
        }
        return all_class_ids;
    }

    std::vector<size_t> class_ids;
    for (size_t index = 0; index < all_labels.size(); ++index) {
        for (const auto &label: labels) {
            if (normalize_label_name(all_labels[index]) == normalize_label_name(label)) {
                class_ids.push_back(index);
                break;
            }
        }
    }

    if (class_ids.empty()) {
        sf::warning(display_branch_category, "No matching class IDs found for labels. Available labels in file:");
        for (size_t index = 0; index < all_labels.size() && index < 10; ++index) {
            sf::warning(display_branch_category, "  [%zu] %s", index, all_labels[index].c_str());
        }
    } else {
        sf::info(display_branch_category, "Label filtering: Found %zu matching class IDs for %zu labels",
                 class_ids.size(), labels.size());
    }

    return class_ids;
}
