// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rectangle.h"
#include "yuv_color.h"

/**
 * @brief Forward declaration for tracked object data.
 */
struct detected_object;

/**
 * @file rectangle_splitter.height
 * @brief Defines data structures and functions for rectangle splitting to handle overlapping objects.
 */

/**
 * @struct split_rectangle
 * @brief Represents a rectangle that has been split from an original detected object.
 */
struct split_rectangle {
    sf::rectangle bounds; ///< The rectangle bounds
    uint32_t original_object_id{}; ///< The original DeepStream object ID
    uint32_t sub_id{}; ///< Sub-ID for this split rectangle (0-based)
    sf::yuv_color color{}; ///< Color to use for drawing borders
    float confidence{}; ///< Confidence level from original detection
    bool was_split{}; ///< Whether this rectangle was actually split from an original

    /**
     * @brief Default constructor.
     */
    split_rectangle() = default;

    /**
     * @brief Constructor with parameters.
     * @param rect_bounds The rectangle bounds
     * @param orig_id The original object ID
     * @param sub_id_param The sub-ID for this split
     * @param rect_color The border color
     * @param conf The confidence level
     * @param split_flag Whether this rectangle was split
     */
    split_rectangle(const sf::rectangle &rect_bounds, const uint32_t orig_id, const uint32_t sub_id_param,
                    const sf::yuv_color rect_color, const float conf = 0.0f, const bool split_flag = false) :
        bounds(rect_bounds),
        original_object_id(orig_id), sub_id(sub_id_param), color(rect_color), confidence(conf), was_split(split_flag) {}

    /**
     * @brief Gets the display ID string - original ID if not split, combined ID if split.
     * @return The display ID string
     */
    [[nodiscard]] std::string get_display_id_string() const {
        if (was_split) {
            return std::to_string(sub_id) + "-" + std::to_string(original_object_id);
        }
        return std::to_string(original_object_id);
    }

    /**
     * @brief Gets the combined ID string in format "subID-originalID" (always).
     * @return The combined ID string
     */
    [[nodiscard]] std::string get_combined_id_string() const {
        return std::to_string(sub_id) + "-" + std::to_string(original_object_id);
    }
};

/**
 * @brief Splits overlapping rectangles from detected objects into non-overlapping regions.
 *
 * This function processes a list of detected objects and identifies overlapping pairs.
 * When overlaps are found, it splits one of the rectangles into smaller rectangles
 * that contain only the non-overlapping parts. The result is a list of rectangles
 * that do not overlap with each other.
 *
 * @param tracked_objects Vector of detected objects to process
 * @param frame_width Width of the frame for boundary checking
 * @param frame_height Height of the frame for boundary checking
 * @param frame_number Current frame number for logging
 * @return Vector of split rectangles with sub-IDs and original object IDs
 */
[[nodiscard]] std::vector<split_rectangle>
split_overlapping_rectangles(const std::vector<detected_object> &tracked_objects, int frame_width, int frame_height,
                             int frame_number);

/**
 * @brief Converts split rectangles to regular rectangles for packing.
 *
 * This function extracts the bounds from split rectangles to create a vector
 * of regular rectangles that can be used with the existing packing algorithm.
 *
 * @param split_rectangles Vector of split rectangles
 * @return Vector of regular rectangles
 */
[[nodiscard]] std::vector<sf::rectangle>
extract_rectangles_from_split_rectangles(const std::vector<split_rectangle> &split_rectangles);

/**
 * @brief Initializes the debug category for rectangle splitting operations.
 */
void init_rectangle_splitter_debug_category();
