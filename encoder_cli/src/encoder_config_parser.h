// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

struct encoder_config;

/**
 * @file encoder_config_parser.h
 * @brief Declares the load-bearing encoder-config parsing concern.
 *
 * Typed readers, string parsers, and the many parameter-group readers live here,
 * separate from the file-loading orchestration in encoder_config_loader.cpp.
 */

/**
 * @brief Applies every config section to the given configuration.
 *
 * Reads all parameter groups (Common, Output, Video, Camera, UDP, Encoder,
 * ROI, Tracker, Detection, Stabilization, Performance, Debug, Logging,
 * Communication, WebRTC, and the ROI data file) from a loaded key file.
 *
 * @param key_file The GKeyFile to read from (must already be loaded).
 * @param config The encoder_config to populate.
 * @param error Pointer to GError for error handling.
 * @throws std::runtime_error if any section fails to parse.
 */
void parse_config_sections(GKeyFile *key_file, encoder_config &config, GError *&error);
