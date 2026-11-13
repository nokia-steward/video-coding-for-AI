#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <gst/gst.h>

/**
 * @brief Trim whitespace from both ends of a string.
 */
[[nodiscard]] std::string trim(const std::string &text);

/**
 * @brief Normalize a label to a canonical token for robust matching.
 */
[[nodiscard]] std::string normalize_label_name(const std::string &label);

/**
 * @brief Split comma-separated labels and trim each token.
 */
[[nodiscard]] std::vector<std::string> split_labels(const std::string &text);

/**
 * @brief Resolve the nvinfer label file path from its config-file-path property.
 */
[[nodiscard]] std::string get_label_file_path(GstElement *nvinfer);

/**
 * @brief Read label lines from a file.
 */
[[nodiscard]] std::vector<std::string> read_label_file(const std::filesystem::path &label_file_path);

/**
 * @brief Map label names to class IDs by reading nvinfer label files.
 */
[[nodiscard]] std::vector<size_t> get_class_ids(GstElement *nvinfer, const std::vector<std::string> &labels);
