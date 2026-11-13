// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../libvcai/include/rectangle.h"
#include "../roi_manager.h"

#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

/**
 * @file extraction.height
 * @brief Declares functions for extracting rectangles with metadata from tracked objects.
 */

/**
 * @brief Extracts rectangles from tracked objects with their metadata.
 * @param tracked_objects The tracked objects
 * @param frame_width The width of the frame
 * @param frame_height The height of the frame
 * @return Vector of rectangles with their associated metadata
 */
[[nodiscard]] std::vector<std::pair<sf::rectangle, std::tuple<uint32_t, sf::yuv_color, float>>>
extract_rectangles_with_metadata(const std::vector<detected_object> &tracked_objects, int frame_width,
                                 int frame_height);
