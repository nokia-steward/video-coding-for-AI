// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../../libvcai/include/logging.h"
#include "../rectangle_splitter.h"
#include "extraction.h"
#include "splitting.h"
#include "utils.h"

#include <algorithm>
#include <gst/gst.h>
#include <map>
#include <ranges>
#include <vector>

/**
 * @file api.cpp
 * @brief Implementation of rectangle splitter API functions.
 */

GST_DEBUG_CATEGORY_EXTERN(rectangle_splitter);


std::vector<split_rectangle> split_overlapping_rectangles(const std::vector<detected_object> &tracked_objects,
                                                          const int frame_width, const int frame_height,
                                                          const int frame_number) {
    std::vector<split_rectangle> result;

    // Extract rectangles with their metadata
    const auto rectangles_with_metadata = extract_rectangles_with_metadata(tracked_objects, frame_width, frame_height);

    if (rectangles_with_metadata.empty()) {
        sf::debug(rectangle_splitter, "[Frame %d] No rectangles to process for splitting", frame_number);
        return result;
    }

    // Skip splitting if there's only one rectangle (no overlaps possible)
    if (rectangles_with_metadata.size() <= 1) {
        // Convert the single rectangle to split_rectangle format without splitting
        for (const auto &[rect, metadata]: rectangles_with_metadata) {
            const auto &[object_id, color, confidence] = metadata;
            result.emplace_back(rect, object_id, 0, color, confidence, false); // was_split = false
        }
        return result;
    }

    // Create a working list of rectangles to process
    std::vector<std::pair<sf::rectangle, std::tuple<uint32_t, sf::yuv_color, float>>> working_rectangles =
            rectangles_with_metadata;

    // Iterate until no more overlaps are found (with safety limit)
    int iteration = 0;
    bool overlaps_found = true;
    constexpr int max_iterations = 10; // Safety limit to prevent infinite loops

    while (overlaps_found && iteration < max_iterations) {
        iteration++;
        overlaps_found = false;
        std::vector processed(working_rectangles.size(), false);

        // Process each rectangle
        for (size_t i = 0; i < working_rectangles.size(); ++i) {
            if (processed[i]) {
                continue; // Skip already processed rectangles
            }

            // Copy rectangle and metadata to avoid use-after-free if vector reallocates
            const auto [current_rect, current_metadata] = working_rectangles[i];
            const auto [current_object_id, current_color, current_confidence] = current_metadata;

            std::vector final_rectangles = {current_rect};

            // Check for overlaps with other rectangles
            for (size_t j = i + 1; j < working_rectangles.size(); ++j) {
                if (processed[j]) {
                    continue; // Skip already processed rectangles
                }

                // Copy rectangle and metadata to avoid use-after-free if vector reallocates
                const auto [other_rect, other_metadata] = working_rectangles[j];
                const auto [other_object_id, other_color, other_confidence] = other_metadata;

                // Check if rectangles overlap (but skip if they're from the same original object)
                if (rectangles_overlap(current_rect, other_rect)) {

                    // Skip overlap detection between rectangles from the same original object
                    if (current_object_id == other_object_id) {
                        continue;
                    }

                    overlaps_found = true; // Mark that we found overlaps in this iteration

                    // Calculate overlap areas to decide which rectangle to split
                    const int current_overlap_area = calculate_overlap_area(current_rect, other_rect);
                    const int other_overlap_area = calculate_overlap_area(other_rect, current_rect);

                    // Check for encompassing scenario: if one rectangle bounds the other from 3+ sides
                    const int current_bounds_sides = count_bounding_sides(current_rect, other_rect);
                    const int other_bounds_sides = count_bounding_sides(other_rect, current_rect);

                    bool split_current;

                    // Priority 1: If one rectangle bounds the other from 3+ sides, split the bounded one
                    if (current_bounds_sides >= 3 && other_bounds_sides < 3) {
                        split_current = false; // Split the other (bounded) rectangle
                    } else if (other_bounds_sides >= 3 && current_bounds_sides < 3) {
                        split_current = true; // Split the current (bounded) rectangle
                    } else {
                        // Priority 2: Split the rectangle with smaller overlap area (less "important" one)
                        // If areas are equal, split the one that appears later in the list
                        split_current = current_overlap_area < other_overlap_area ||
                                        (current_overlap_area == other_overlap_area && i < j);
                    }

                    if (split_current) {
                        // Split current rectangle - update the working list
                        std::vector<sf::rectangle> new_rectangles;
                        for (const auto &rect: final_rectangles) {
                            const auto split_result =
                                    split_rectangle_by_overlap(rect, other_rect, frame_width, frame_height);
                            new_rectangles.insert(new_rectangles.end(), split_result.begin(), split_result.end());
                        }
                        final_rectangles = std::move(new_rectangles);

                        // Update the working rectangles list with the split results
                        working_rectangles[i].first =
                                final_rectangles.empty() ? sf::rectangle{0, 0, 0, 0} : final_rectangles[0];

                        // Add additional split rectangles to the working list
                        for (size_t k = 1; k < final_rectangles.size(); ++k) {
                            working_rectangles.emplace_back(final_rectangles[k], current_metadata);
                            processed.push_back(false);
                        }
                    } else {
                        // Split other rectangle
                        const auto split_result =
                                split_rectangle_by_overlap(other_rect, current_rect, frame_width, frame_height);
                        working_rectangles[j].first =
                                split_result.empty() ? sf::rectangle{0, 0, 0, 0} : split_result[0];

                        // Add additional split rectangles to the working list
                        for (size_t k = 1; k < split_result.size(); ++k) {
                            working_rectangles.emplace_back(split_result[k], other_metadata);
                            processed.push_back(false);
                        }
                    }
                }
            }

            processed[i] = true;
        }

        // If no overlaps were found in this iteration, we're done
        if (!overlaps_found) {
            break;
        }
    }

    // Check if we hit the iteration limit
    if (iteration >= max_iterations && overlaps_found) {
        sf::warning(rectangle_splitter,
                    "[Frame %d] Reached maximum iterations (%d), stopping splitting to prevent infinite loop",
                    frame_number, max_iterations);
    }

    // Convert all working rectangles to split_rectangle format with proper sub IDs
    // Group rectangles by original object ID to assign proper sub IDs
    std::map<uint32_t, std::vector<std::pair<sf::rectangle, std::tuple<uint32_t, sf::yuv_color, float>>>>
            grouped_rectangles;

    for (auto [rect, metadata]: working_rectangles) {
        const auto &[object_id, color, confidence] = metadata;

        if (rect.is_valid()) {
            grouped_rectangles[object_id].emplace_back(rect, metadata);
        }
    }

    // Create split rectangles with proper sub IDs
    for (const auto &rect_list: grouped_rectangles | std::views::values) {
        const bool was_split = rect_list.size() > 1;

        for (size_t sub_id = 0; sub_id < rect_list.size(); ++sub_id) {
            const auto &[rect, metadata] = rect_list[sub_id];
            const auto &[orig_object_id, color, confidence] = metadata;

            result.emplace_back(rect, orig_object_id, static_cast<uint32_t>(sub_id), color, confidence, was_split);
        }
    }

    return result;
}
