// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "roi_parser.h"

#include <charconv>
#include <fstream>

#include <algorithm>
#include <array>
#include <gst/gst.h>
#include "../../libvcai/include/logging.h"

/**
 * @file roi_parser.cpp
 * @brief Implementation of ROI data file parsing and sequence configuration loading.
 */

/// GStreamer debug category for ROI parsing.
GST_DEBUG_CATEGORY_STATIC(roi_parser);

void init_roi_parser_debug_category() {
    GST_DEBUG_CATEGORY_INIT(roi_parser, "roi-parser", 0, "Parsing of ROI data files and sequence configuration");
}

namespace {
    /**
     * @brief A helper struct to hold the components of a parsed filename.
     */
    struct parsed_filename_info {
        std::string_view sequence_name;
        int width;
        int height;
        int frame_rate;
    };

    /**
     * @brief Safely converts a string_view to an integer.
     * @param sv The string_view to convert.
     * @return An optional containing the integer, or nullopt if conversion fails.
     */
    std::optional<int> string_to_int(const std::string_view sv) {
        int value{};
        if (const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
            ec == std::errc() && ptr == sv.data() + sv.size()) {
            return value;
        }
        return std::nullopt;
    }

    /**
     * @brief Parses a specially formatted ROI filename.
     *
     * The expected filename format is: SequenceName_WidthxHeight_FrameRate.ext
     * Example: "BasketballDrill_832x480_50.txt"
     *
     * @param filename The filename to parse.
     * @return A struct containing the parsed components, or std::nullopt if the format is incorrect.
     */
    std::optional<parsed_filename_info> parse_roi_filename(const std::string_view filename) {
        const auto stem = filename.substr(0, filename.find_last_of('.'));

        const auto fr_sep_pos = stem.find_last_of('_');
        if (fr_sep_pos == std::string_view::npos) {
            return std::nullopt;
        }
        const auto fr_str = stem.substr(fr_sep_pos + 1);
        const auto stem_without_fr = stem.substr(0, fr_sep_pos);

        const auto res_sep_pos = stem_without_fr.find_last_of('_');
        if (res_sep_pos == std::string_view::npos) {
            return std::nullopt;
        }
        const auto res_str = stem_without_fr.substr(res_sep_pos + 1);
        const auto name_sv = stem_without_fr.substr(0, res_sep_pos);

        const auto separator_x_position = res_str.find('x');
        if (separator_x_position == std::string_view::npos) {
            return std::nullopt;
        }
        const auto width_str = res_str.substr(0, separator_x_position);
        const auto height_str = res_str.substr(separator_x_position + 1);

        const auto frame_rate = string_to_int(fr_str);
        const auto width = string_to_int(width_str);
        const auto height = string_to_int(height_str);

        if (!frame_rate || !width || !height) {
            return std::nullopt;
        }

        return parsed_filename_info{name_sv, *width, *height, *frame_rate};
    }

    /**
     * @brief Parses a string_view of comma-separated numbers into an array of 4 integers.
     * @param sv The string_view to parse, e.g., "12,11,253,216".
     * @return An optional containing the array, or nullopt on failure.
     */
    std::optional<std::array<int, 4>> parse_coords(const std::string_view sv) {
        std::array<int, 4> coords{};
        size_t coord_idx = 0;
        size_t num_start = 0;

        for (size_t char_index = 0; char_index <= sv.length(); ++char_index) {
            if (char_index == sv.length() || sv[char_index] == ',') {
                if (coord_idx >= 4) {
                    return std::nullopt; // Too many numbers
                }

                const auto num_sv = sv.substr(num_start, char_index - num_start);
                const auto val_opt = string_to_int(num_sv);
                if (!val_opt) {
                    return std::nullopt; // Not a valid integer
                }

                coords[coord_idx++] = *val_opt;
                num_start = char_index + 1;
            }
        }
        if (coord_idx != 4) {
            return std::nullopt; // Not enough numbers
        }
        return coords;
    }

    /**
     * @brief Parses a string_view representing all ROIs for a single frame.
     * @param sv The string_view to parse, e.g., "[12,11,253,216],[87,0,159,41]".
     * @return An optional containing a vector of roi_rects, or nullopt on failure.
     */
    std::optional<std::vector<sf::rectangle>> parse_rois_for_frame(const std::string_view sv) {
        std::vector<sf::rectangle> rois;
        size_t pos = 0;
        while (pos < sv.length()) {
            if (sv[pos] == ',') {
                pos++; // Skip separator between ROIs
            }

            if (pos >= sv.length() || sv[pos] != '[') {
                return std::nullopt; // Expected ROI start
            }
            const size_t roi_end = sv.find(']', pos);
            if (roi_end == std::string_view::npos) {
                return std::nullopt; // Expected ROI end
            }

            const auto roi_content_sv = sv.substr(pos + 1, roi_end - (pos + 1));
            const auto coords_opt = parse_coords(roi_content_sv);
            if (!coords_opt) {
                return std::nullopt;
            }

            const auto &coords = *coords_opt;
            const int horizontal_coord_start = coords[0];
            const int vertical_coord_start = coords[1];
            const int horizontal_coord_end = coords[2];
            const int vertical_coord_end = coords[3];

            if (horizontal_coord_end < horizontal_coord_start || vertical_coord_end < vertical_coord_start) {
                sf::warning(roi_parser,
                            "ROI with invalid coordinates (horizontal_coord_end < horizontal_coord_start or "
                            "vertical_coord_end < vertical_coord_start): [%d,%d,%d,%d]",
                            horizontal_coord_start, vertical_coord_start, horizontal_coord_end, vertical_coord_end);
            }
            rois.emplace_back(horizontal_coord_start, vertical_coord_start,
                              horizontal_coord_end - horizontal_coord_start, vertical_coord_end - vertical_coord_start);
            pos = roi_end + 1;
        }
        return rois;
    }
} // namespace

/**
 * @brief Parses ROI content from an input stream.
 * @param file_stream Input stream containing ROI data.
 * @param temporal_resampling Frame index multiplier.
 * @return Parsed ROI data or nullopt on failure.
 */
std::optional<roi_data> parse_roi_content(std::istream &file_stream, const int temporal_resampling) {
    if (!file_stream.good()) {
        sf::error(roi_parser, "Stream is not in a good state for reading.");
        return std::nullopt;
    }

    // Read file content line by line to avoid istreambuf_iterator issues
    std::string content;
    std::string line;
    while (std::getline(file_stream, line)) {
        content += line;
    }

    if (file_stream.bad()) {
        sf::error(roi_parser, "A critical I/O error occurred while reading ROI file.");
        return std::nullopt;
    }
    // Remove all whitespace characters
    content.erase(std::ranges::remove_if(content, isspace).begin(), content.end());

    // Expect format like [[[...],[...]],[[...]],...]
    if (!content.starts_with("[[") || !content.ends_with("]]")) {
        sf::error(roi_parser, "ROI file content has invalid format. Expected to be enclosed in [[...]].");
        return std::nullopt;
    }

    std::string_view sv = content;
    sv.remove_prefix(1); // remove leading "["
    sv.remove_suffix(1); // remove trailing "]"

    roi_data data;
    int frame_idx = 0;
    size_t pos = 0;

    while (pos < sv.length()) {
        if (sv[pos] != '[') {
            sf::error(roi_parser, "Parse error: expected '[' at start of frame %d.", frame_idx);
            return std::nullopt;
        }

        // Find matching ']' for the frame, respecting nested brackets for ROIs
        int bracket_level = 1;
        size_t frame_end = pos + 1;
        while (frame_end < sv.length() && bracket_level > 0) {
            if (sv[frame_end] == '[') {
                bracket_level++;
            } else if (sv[frame_end] == ']') {
                bracket_level--;
            }
            frame_end++;
        }

        if (bracket_level != 0) {
            sf::error(roi_parser, "Parse error: Mismatched brackets for frame %d.", frame_idx);
            return std::nullopt;
        }
        frame_end--; // move back to the closing bracket

        // sv for one frame's ROIs, e.g., "[...],[...]"
        const auto frame_content_sv = sv.substr(pos + 1, frame_end - (pos + 1));

        const auto rois_opt = parse_rois_for_frame(frame_content_sv);
        if (!rois_opt) {
            sf::error(roi_parser, "Parse error: Could not parse ROIs for frame %d.", frame_idx);
            return std::nullopt;
        }

        if (!rois_opt->empty()) {
            sf::debug(roi_parser, "Parsed %zu ROIs for frame %d.", rois_opt->size(), frame_idx);
            data[frame_idx] = *rois_opt;
        }

        pos = frame_end + 1;
        if (pos < sv.length() && sv[pos] == ',') {
            pos++; // Skip separator between frames
        }
        frame_idx += temporal_resampling;
    }

    return data;
}

/**
 * @brief Loads ROI file and associated sequence configuration.
 * @param file_path Path to the ROI file.
 * @param temporal_resampling Frame index multiplier.
 * @return Parsed sequence information or nullopt on failure.
 */
std::optional<roi_sequence_info> load_roi_file(const std::filesystem::path &file_path, const int temporal_resampling) {
    GST_DEBUG_CATEGORY_INIT(roi_parser, "roi-parser", 0, "Parses ROI data files");

    if (temporal_resampling <= 0) {
        sf::error(roi_parser, "Temporal resampling factor must be positive, but got %d.", temporal_resampling);
        return std::nullopt;
    }

    if (!std::filesystem::exists(file_path)) {
        sf::error(roi_parser, "ROI file not found at %s", file_path.c_str());
        return std::nullopt;
    }

    const auto filename_str = file_path.filename().string();
    const auto parsed_name_opt = parse_roi_filename(filename_str);

    if (!parsed_name_opt) {
        sf::error(roi_parser, "Could not parse filename format: %s", filename_str.c_str());
        return std::nullopt;
    }
    const auto &parsed_name = *parsed_name_opt;

    roi_sequence_info info;
    info.sequence_name = parsed_name.sequence_name;
    info.width = parsed_name.width;
    info.height = parsed_name.height;
    info.frame_rate_nominal = parsed_name.frame_rate;

    // Look up configuration from sfu_config.height
    if (const auto it = fr_dict.find(info.sequence_name); it != fr_dict.end()) {
        info.frame_rate_details = it->second;
    } else {
        sf::warning(roi_parser, "Frame rate details not found in sfu_config for %s", info.sequence_name.c_str());
    }

    if (const auto it = seq_cfg.find(info.sequence_name); it != seq_cfg.end()) {
        info.qp_config_ld = it->second;
    } else {
        sf::warning(roi_parser, "Low-delay QP config not found in sfu_config for %s", info.sequence_name.c_str());
    }

    if (const auto it = seq_cfg_ai.find(info.sequence_name); it != seq_cfg_ai.end()) {
        info.qp_config_ai = it->second;
    } else {
        sf::warning(roi_parser, "All-Intra QP config not found in sfu_config for %s", info.sequence_name.c_str());
    }

    std::ifstream file_stream(file_path);
    if (!file_stream) {
        sf::error(roi_parser, "Failed to open ROI file for reading: %s", file_path.c_str());
        return std::nullopt;
    }

    auto roi_data_opt = parse_roi_content(file_stream, temporal_resampling);
    if (!roi_data_opt) {
        sf::error(roi_parser, "Failed to parse content of ROI file: %s", file_path.c_str());
        return std::nullopt;
    }

    info.rois = std::move(*roi_data_opt);

    return info;
}
