// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../../libvcai/include/logging.h"
#include "../../libvcai/include/rectangle.h"

/**
 * @file groundtruth_roi_data.height
 * @brief Declares structures and functions for loading per-frame ROI data from COCO-format groundtruth JSON files.
 *
 * COCO groundtruth files contain "images", "annotations", and optionally "categories" arrays.
 * Each annotation provides a bounding box [x, y, width, height] keyed to an image_id.
 * This module parses the JSON, maps image entries to sequential frame indices
 * (or extracts them from filenames when available), and provides per-frame rectangle lookup.
 */

/**
 * @brief Container for per-frame groundtruth ROI data loaded from a COCO JSON file.
 *
 * Stores a mapping from frame index to a vector of sf::rectangle bounding boxes.
 * Frame indices are derived either from the image filename suffix (e.g. "frame_042.jpg" -> 42)
 * or from the positional order in the "images" array when filenames lack a parseable index.
 */
struct groundtruth_roi_data {
    /// @brief Per-frame bounding boxes. Key is the 0-based frame index.
    std::unordered_map<int, std::vector<sf::rectangle>> frames;

    /**
     * @brief Default constructor creates an empty frame map.
     */
    groundtruth_roi_data() : frames{} {}

    /**
     * @brief Constructor that takes a pre-built frame map.
     * @param frame_map Per-frame rectangle map to initialize with
     */
    explicit groundtruth_roi_data(std::unordered_map<int, std::vector<sf::rectangle>> frame_map) :
        frames(std::move(frame_map)) {}

    /**
     * @brief Get the number of frames with groundtruth data.
     * @return Number of frames
     */
    [[nodiscard]] size_t size() const { return frames.size(); }

    /**
     * @brief Check if there is no groundtruth data.
     * @return true if no frames are present, false otherwise
     */
    [[nodiscard]] bool empty() const { return frames.empty(); }

    /**
     * @brief Get rectangles for a given frame index.
     * @param frame_index 0-based frame index
     * @return Vector of rectangles for that frame, or empty vector if no data exists
     */
    [[nodiscard]] std::vector<sf::rectangle> get_rectangles(const int frame_index) const {
        const auto iter = frames.find(frame_index);
        if (iter != frames.end()) {
            return iter->second;
        }
        return {};
    }

    /**
     * @brief Get the total number of annotations across all frames.
     * @return Total annotation count
     */
    [[nodiscard]] size_t total_annotations() const {
        size_t count = 0;
        for (const auto &[frame_index, rects]: frames) {
            count += rects.size();
        }
        return count;
    }
};

/**
 * @brief Loads per-frame groundtruth ROI data from a COCO-format JSON file.
 *
 * The COCO JSON must contain at minimum:
 * - "images": array of objects with "id" and "file_name" fields
 * - "annotations": array of objects with "image_id", "bbox" [x, y, w, h], and "category_id"
 *
 * Frame indices are derived from image filenames when they end with "_NNN.ext",
 * otherwise positional order in the images array is used.
 *
 * @param file_path Path to the COCO JSON file
 * @return An optional containing the parsed groundtruth ROI data, or std::nullopt on error
 */
[[nodiscard]] std::optional<groundtruth_roi_data> load_groundtruth_roi_data(const std::filesystem::path &file_path);

/**
 * @brief Initializes the GStreamer debug category for groundtruth ROI data processing.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_groundtruth_roi_debug_category();

/**
 * @brief Gets the groundtruth ROI debug category.
 * @return Pointer to the groundtruth ROI debug category.
 */
[[nodiscard]] GstDebugCategory *get_groundtruth_roi_debug_category();
