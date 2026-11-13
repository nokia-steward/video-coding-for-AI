// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "rectangle.h"
#include "sfu_config.h"

/**
 * @file roi_parser.height
 * @brief Declares structures and functions for parsing ROI data files.
 */

/**
 * @brief A map holding ROI data for a video sequence, keyed by frame number.
 *
 * Each frame number maps to a vector of rectangles defining the ROIs for that frame.
 */
using roi_data = std::map<int, std::vector<sf::rectangle>>;

/**
 * @brief Holds all information related to a sequence, parsed from an ROI file and sfu_config.
 */
struct roi_sequence_info {
    // --- Data from filename ---
    std::string sequence_name{};
    int width{};
    int height{};
    int frame_rate_nominal{};

    // --- Data from sfu_config.height ---
    std::optional<frame_rate_info> frame_rate_details{};
    std::optional<qp_settings> qp_config_ld{};
    std::optional<qp_settings> qp_config_ai{};

    // --- Data from file content ---
    roi_data rois{};
};

/**
 * @brief Parses the content of an ROI file stream.
 *
 * The expected format is a single line representing a Python-style list of lists of lists:
 * [[[x1,y1,x2,y2], ...], [[x1,y1,x2,y2], ...], ...]
 * Each top-level list corresponds to a frame, with the frame number being its index.
 * Each inner list is an ROI defined by top-left (x1,y1) and bottom-right (x2,y2) coordinates.
 * The function calculates width and height from these coordinates.
 *
 * @param file_stream An input stream containing the ROI data.
 * @param temporal_resampling The factor by which to multiply the frame index. For a factor N,
 *        the frame indices in the map will be 0, N, 2N, ...
 * @return An optional containing the parsed roi_data map, or std::nullopt on parsing error.
 */
[[nodiscard]] std::optional<roi_data> parse_roi_content(std::istream &file_stream, int temporal_resampling);

/**
 * @brief Loads and parses an ROI file and its associated configuration.
 *
 * This function orchestrates the entire process:
 * 1. Parses the filename to extract sequence name, resolution, and framerate.
 * 2. Uses the sequence name to look up configuration data from `sfu_config.height`.
 * 3. Reads the ROI file to get the regions of interest for each frame.
 * 4. Combines all information into a single `roi_sequence_info` struct.
 *
 * @param file_path The path to the ROI file (e.g., "BasketballDrill_832x480_50.txt").
 * @param temporal_resampling The temporal resampling factor to apply when parsing frame indices.
 * @return An optional containing the fully populated `roi_sequence_info` struct,
 *         or std::nullopt if any step fails (file not found, parsing error, config not found).
 */
[[nodiscard]] std::optional<roi_sequence_info> load_roi_file(const std::filesystem::path &file_path,
                                                             int temporal_resampling);

/**
 * @brief Initializes the GStreamer debug category for ROI parser.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_roi_parser_debug_category();
