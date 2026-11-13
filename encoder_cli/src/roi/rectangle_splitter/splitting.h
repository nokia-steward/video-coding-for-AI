// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <vector>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file splitting.height
 * @brief Declares rectangle splitting algorithms.
 */

/**
 * @brief Splits a rectangle by removing the overlapping area with another rectangle.
 * This function takes a rectangle and splits it into smaller rectangles that don't
 * overlap with the given blocking rectangle. The algorithm creates up to 4 new
 * rectangles representing the non-overlapping parts.
 *
 * @param rect_to_split The rectangle to split
 * @param blocking_rect The rectangle that blocks the area to remove
 * @param frame_width The width of the frame for even dimension adjustment
 * @param frame_height The height of the frame for even dimension adjustment
 * @return Vector of non-overlapping rectangles with even dimensions
 */
[[nodiscard]] std::vector<sf::rectangle> split_rectangle_by_overlap(const sf::rectangle &rect_to_split,
                                                                    const sf::rectangle &blocking_rect, int frame_width,
                                                                    int frame_height);
