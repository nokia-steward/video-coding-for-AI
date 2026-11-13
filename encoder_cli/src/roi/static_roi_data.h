// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "../../libvcai/include/logging.h"
#include "rectangle.h"

/**
 * @file static_roi_data.height
 * @brief Declares structures and functions for loading and accessing static ROI data from JSON files.
 */

/**
 * @brief Data structure representing a single ROI from the static JSON format.
 *
 * This struct represents a region of interest with position and dimensions.
 * It maps directly to the JSON structure with x, y, width, and height fields.
 */
struct static_roi_rect {
    int horizontal_coord; // X coordinate (left edge)
    int vertical_coord; // Y coordinate (top edge)
    int width; // Width of the ROI
    int height; // Height of the ROI

    /**
     * @brief Default constructor initializes all fields to zero.
     */
    static_roi_rect() : horizontal_coord(0), vertical_coord(0), width(0), height(0) {}

    /**
     * @brief Parameterized constructor.
     * @param x_pos X coordinate of the left edge
     * @param y_pos Y coordinate of the top edge
     * @param w Width of the ROI
     * @param h Height of the ROI
     */
    static_roi_rect(const int x_pos, const int y_pos, const int w, const int h) :
        horizontal_coord(x_pos), vertical_coord(y_pos), width(w), height(h) {}

    /**
     * @brief Convert to sf::rectangle format.
     * @return A rectangle with the same position and dimensions
     */
    [[nodiscard]] sf::rectangle to_rectangle() const {
        return sf::rectangle(horizontal_coord, vertical_coord, width, height);
    }

    /**
     * @brief Check if the ROI is valid (positive dimensions).
     * @return true if width and height are positive, false otherwise
     */
    [[nodiscard]] bool is_valid() const { return width > 0 && height > 0; }
};

/**
 * @brief Container for static ROI data loaded from JSON.
 *
 * This struct holds the complete static ROI configuration loaded from a JSON file.
 * It contains a vector of ROIs that can be used for static region processing.
 */
struct static_roi_data {
    std::vector<static_roi_rect> rois; // Vector of static ROIs

    /**
     * @brief Default constructor creates an empty ROI list.
     */
    static_roi_data() : rois{} {}

    /**
     * @brief Constructor that takes a vector of ROIs.
     * @param roi_list Vector of static ROIs to initialize with
     */
    explicit static_roi_data(std::vector<static_roi_rect> roi_list) : rois(std::move(roi_list)) {}

    /**
     * @brief Get the number of ROIs in this data structure.
     * @return Number of ROIs
     */
    [[nodiscard]] size_t size() const { return rois.size(); }

    /**
     * @brief Check if the data structure is empty.
     * @return true if no ROIs are present, false otherwise
     */
    [[nodiscard]] bool empty() const { return rois.empty(); }

    /**
     * @brief Convert all ROIs to sf::rectangle format.
     * @return Vector of rectangles representing the ROIs
     */
    [[nodiscard]] std::vector<sf::rectangle> to_rectangles() const {
        std::vector<sf::rectangle> rectangles;
        rectangles.reserve(rois.size());

        for (const auto &roi: rois) {
            rectangles.push_back(roi.to_rectangle());
        }

        return rectangles;
    }
};

/**
 * @brief Loads static ROI data from a JSON file.
 *
 * This function reads a JSON file containing static ROI data in the format:
 * {
 *   "rois": [
 *     {"x": 0, "y": 0, "width": 640, "height": 480},
 *     ...
 *   ]
 * }
 *
 * @param file_path Path to the JSON file containing static ROI data
 * @return An optional containing the parsed static ROI data, or std::nullopt on error
 */
[[nodiscard]] std::optional<static_roi_data> load_static_roi_data(const std::filesystem::path &file_path);

/**
 * @brief Initializes the GStreamer debug category for static ROI data processing.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_static_roi_debug_category();

/**
 * @brief Gets the static ROI debug category.
 * @return Pointer to the static ROI debug category.
 */
[[nodiscard]] GstDebugCategory *get_static_roi_debug_category();
