#pragma once

#include <string>

#include <gst/gst.h>

/**
 * @brief Configure nvinfer from a DeepStream YAML config file.
 */
[[nodiscard]] bool configure_nvinfer(GstElement *nvinfer, const std::string &config_file);

/**
 * @brief Resolve tracker config path, falling back to DeepStream defaults.
 */
[[nodiscard]] std::string determine_tracker_config_path(const std::string &config_file);

/**
 * @brief Configure nvtracker with library, config and dimensions.
 */
[[nodiscard]] bool configure_nvtracker(GstElement *nvtracker, const std::string &config_file,
                                       const std::string &lib_file, int width, int height);
