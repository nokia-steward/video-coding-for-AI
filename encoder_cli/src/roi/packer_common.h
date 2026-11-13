// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include <vector>
#include "rectangle.h"

/**
 * @file packer_common.height
 * @brief Common types and structures shared between different packer implementations.
 */

/**
 * @brief The result of a packing operation.
 */
struct pack_result {
    std::vector<sf::rectangle> sorted_source_rectangles;
    std::vector<sf::rectangle> packed_rectangles;
    sf::rectangle result_size;
    // Mapping from original ROI index to packed ROI index (for merged ROIs)
    // If empty, assume 1:1 mapping (no merging occurred)
    std::vector<size_t> original_to_packed_mapping;
};

/**
 * @brief A rectangle with an identifier for position stability tracking.
 */
struct stable_rectangle {
    sf::rectangle rect;
    std::string id; // Unique identifier for position stability
    std::string representative_id; // Stable representative ID for merge groups

    stable_rectangle(const sf::rectangle &r, std::string identifier) :
        rect(r), id(std::move(identifier)), representative_id(id) {}

    stable_rectangle(const sf::rectangle &r, std::string identifier, std::string representative) :
        rect(r), id(std::move(identifier)), representative_id(std::move(representative)) {}

    stable_rectangle(const int x, const int y, const int w, const int h, std::string identifier) :
        rect(x, y, w, h), id(std::move(identifier)), representative_id(id) {}

    stable_rectangle(const int x, const int y, const int w, const int h, std::string identifier,
                     std::string representative) :
        rect(x, y, w, h),
        id(std::move(identifier)), representative_id(std::move(representative)) {}
};

/**
 * @brief The result of a stable packing operation.
 */
struct stable_pack_result {
    std::vector<sf::rectangle> packed_rectangles;
    sf::rectangle result_size;
    size_t stable_count; // Number of rectangles that kept their previous positions
    size_t repacked_count; // Number of rectangles that were repacked
};
