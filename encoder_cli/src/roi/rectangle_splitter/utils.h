// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../libvcai/include/rectangle.h"

/**
 * @file utils.height
 * @brief Declares utility functions for rectangle splitter.
 */

/**
 * @brief Checks if two rectangles overlap.
 * @param rect1 First rectangle
 * @param rect2 Second rectangle
 * @return true if rectangles overlap, false otherwise
 */
[[nodiscard]] bool rectangles_overlap(const sf::rectangle &rect1, const sf::rectangle &rect2);

/**
 * @brief Calculates the overlap area between two rectangles.
 * @param rect1 First rectangle
 * @param rect2 Second rectangle
 * @return The area of overlap, or 0 if no overlap
 */
[[nodiscard]] int calculate_overlap_area(const sf::rectangle &rect1, const sf::rectangle &rect2);

/**
 * @brief Counts how many sides of rect2 are bounded by rect1.
 * @param rect1 The potentially bounding rectangle
 * @param rect2 The potentially bounded rectangle
 * @return Number of sides (0-4) that rect1 bounds rect2 from
 */
[[nodiscard]] int count_bounding_sides(const sf::rectangle &rect1, const sf::rectangle &rect2);
