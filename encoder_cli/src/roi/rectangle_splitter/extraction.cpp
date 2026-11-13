// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "extraction.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../../libvcai/include/rectangle.h"
#include "../../processing/roi_detection_utils.h"
#include "../rectangle_splitter.h"
#include "../roi_manager.h"
#include "validation.h"

#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

/**
 * @file extraction.cpp
 * @brief Implementation of rectangle extraction with metadata for rectangle splitter.
 */

GST_DEBUG_CATEGORY_EXTERN(rectangle_splitter);

std::vector<std::pair<sf::rectangle, std::tuple<uint32_t, sf::yuv_color, float>>>
extract_rectangles_with_metadata(const std::vector<detected_object> &tracked_objects, const int frame_width,
                                 const int frame_height) {
    std::vector<std::pair<sf::rectangle, std::tuple<uint32_t, sf::yuv_color, float>>> result;

    size_t red_count = 0;
    size_t blue_count = 0;
    size_t purple_count = 0;

    for (const auto &obj: tracked_objects) {
        if (!obj.history.empty()) {
            const auto &latest_data = obj.history.back();
            sf::rectangle rect;
            rect.left = latest_data.bounds.left;
            rect.top = latest_data.bounds.top;
            rect.width = latest_data.bounds.width;
            rect.height = latest_data.bounds.height;

            // Only add rectangles that are valid (have positive dimensions) and ensure even dimensions
            if (const auto clipped_rect = clip_rectangle_to_frame(rect, frame_width, frame_height);
                is_valid_rectangle(clipped_rect, frame_width, frame_height)) {
                const auto even_rect = ensure_even_rectangle(clipped_rect, frame_width, frame_height);
                result.emplace_back(even_rect,
                                    std::make_tuple(obj.object_id, latest_data.color, latest_data.confidence));

                // Count colors for logging
                if (latest_data.color == sf::yuv_color::red) {
                    red_count++;
                } else if (latest_data.color == sf::yuv_color::blue) {
                    blue_count++;
                } else if (latest_data.color == sf::yuv_color::purple) {
                    purple_count++;
                }
            }
        }
    }

    // Log summary of extracted colors
    if (red_count > 0 || blue_count > 0 || purple_count > 0) {
        sf::info(rectangle_splitter,
                 "[Extraction] Extracted %zu rectangles from tracked objects: %zu RED (predicted), %zu BLUE "
                 "(detected), %zu PURPLE (low confidence)",
                 result.size(), red_count, blue_count, purple_count);
    }

    return result;
}

std::vector<sf::rectangle>
extract_rectangles_from_split_rectangles(const std::vector<split_rectangle> &split_rectangles) {
    std::vector<sf::rectangle> result;
    result.reserve(split_rectangles.size());

    for (const auto &split_rect: split_rectangles) {
        result.push_back(split_rect.bounds);
    }

    return result;
}
