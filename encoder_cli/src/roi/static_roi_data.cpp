// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "static_roi_data.h"

#include <fstream>
#include <gst/gst.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#endif
#include <json/json.h>
#include "../../libvcai/include/logging.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/**
 * @file static_roi_data.cpp
 * @brief Implementation of static ROI data loading from JSON files.
 */

/// GStreamer debug category for static ROI parsing.
GST_DEBUG_CATEGORY_STATIC(static_roi);

void init_static_roi_debug_category() {
    GST_DEBUG_CATEGORY_INIT(static_roi, "static-roi", 0, "Loading and processing static ROI data from JSON files");
}

GstDebugCategory *get_static_roi_debug_category() { return static_roi; }

namespace {
    /**
     * @brief Validates that a JSON object contains the required ROI fields.
     * @param roi_json The JSON object representing a single ROI
     * @return true if all required fields are present and valid, false otherwise
     */
    bool validate_roi_json(const Json::Value &roi_json) {
        if (!roi_json.isObject()) {
            sf::error(static_roi, "ROI entry is not a JSON object");
            return false;
        }

        // Check for required fields
        for (const std::vector<std::string> required_fields = {"x", "y", "width", "height"};
             const auto &field: required_fields) {
            if (!roi_json.isMember(field)) {
                sf::error(static_roi, "ROI entry missing required field: %s", field.c_str());
                return false;
            }
            if (!roi_json[field].isInt()) {
                sf::error(static_roi, "ROI field '%s' is not an integer", field.c_str());
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Parses a single ROI from JSON.
     * @param roi_json The JSON object representing a single ROI
     * @return An optional containing the parsed static_roi_rect, or std::nullopt on error
     */
    std::optional<static_roi_rect> parse_single_roi(const Json::Value &roi_json) {
        if (!validate_roi_json(roi_json)) {
            return std::nullopt;
        }

        const int horizontal_coord = roi_json["x"].asInt();
        const int vertical_coord = roi_json["y"].asInt();
        const int width = roi_json["width"].asInt();
        const int height = roi_json["height"].asInt();

        // Validate that dimensions are positive
        if (width <= 0 || height <= 0) {
            sf::warning(static_roi, "ROI has invalid dimensions: width=%d, height=%d", width, height);
            return std::nullopt;
        }

        // Validate that coordinates are non-negative
        if (horizontal_coord < 0 || vertical_coord < 0) {
            sf::warning(static_roi, "ROI has negative coordinates: x=%d, y=%d", horizontal_coord, vertical_coord);
            return std::nullopt;
        }

        return static_roi_rect(horizontal_coord, vertical_coord, width, height);
    }
} // namespace

/**
 * @brief Loads static ROI data from a JSON file.
 * @param file_path Path to the JSON file.
 * @return Parsed static ROI data or nullopt on error.
 */
std::optional<static_roi_data> load_static_roi_data(const std::filesystem::path &file_path) {
    GST_DEBUG_CATEGORY_INIT(static_roi, "static-roi", 0, "Loading static ROI data from JSON");

    if (!std::filesystem::exists(file_path)) {
        sf::error(static_roi, "Static ROI file not found at %s", file_path.c_str());
        return std::nullopt;
    }

    std::ifstream file_stream(file_path);
    if (!file_stream.is_open()) {
        sf::error(static_roi, "Failed to open static ROI file for reading: %s", file_path.c_str());
        return std::nullopt;
    }

    Json::Value json_data;
    std::string parse_errors;

    if (Json::CharReaderBuilder reader_builder;
        !Json::parseFromStream(reader_builder, file_stream, &json_data, &parse_errors)) {
        sf::error(static_roi, "Failed to parse JSON file %s: %s", file_path.c_str(), parse_errors.c_str());
        return std::nullopt;
    }

    // Validate root structure
    if (!json_data.isObject()) {
        sf::error(static_roi, "JSON root is not an object");
        return std::nullopt;
    }

    if (!json_data.isMember("rois")) {
        sf::error(static_roi, "JSON file missing required 'rois' field");
        return std::nullopt;
    }

    if (!json_data["rois"].isArray()) {
        sf::error(static_roi, "'rois' field is not an array");
        return std::nullopt;
    }

    const auto &rois_array = json_data["rois"];
    if (rois_array.empty()) {
        sf::warning(static_roi, "No ROIs found in JSON file");
        return static_roi_data{}; // Return empty data structure
    }

    std::vector<static_roi_rect> parsed_rois;
    parsed_rois.reserve(rois_array.size());

    for (Json::ArrayIndex i = 0; i < rois_array.size(); ++i) {
        const auto roi_opt = parse_single_roi(rois_array[i]);
        if (!roi_opt) {
            sf::error(static_roi, "Failed to parse ROI at index %u", i);
            return std::nullopt;
        }

        parsed_rois.push_back(*roi_opt);
    }

    return static_roi_data{std::move(parsed_rois)};
}
