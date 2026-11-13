// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "utils.h"
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file utils.cpp
 * @brief Utility functions for rectangle splitter.
 */

/**
 * @brief Checks if two rectangles overlap.
 * @param rect1 First rectangle.
 * @param rect2 Second rectangle.
 * @return true if rectangles overlap, false otherwise.
 */
bool rectangles_overlap(const sf::rectangle &rect1, const sf::rectangle &rect2) { return rect1.intersects(rect2); }

/**
 * @brief Calculates the overlap area between two rectangles.
 * @param rect1 First rectangle.
 * @param rect2 Second rectangle.
 * @return Area of the intersection.
 */
int calculate_overlap_area(const sf::rectangle &rect1, const sf::rectangle &rect2) {
    const auto intersection = rect1.intersection(rect2);
    return intersection.area();
}

/**
 * @brief Counts how many sides of rect2 are bounded by rect1.
 * @param rect1 Outer rectangle candidate.
 * @param rect2 Inner rectangle candidate.
 * @return Number of bounded sides (0-4).
 */
int count_bounding_sides(const sf::rectangle &rect1, const sf::rectangle &rect2) {
    int bounded_sides = 0;

    // Check left side: rect1's left edge is at or to the left of rect2's left edge
    if (rect1.left <= rect2.left) {
        bounded_sides++;
    }

    // Check right side: rect1's right edge is at or to the right of rect2's right edge
    if (rect1.left + rect1.width >= rect2.left + rect2.width) {
        bounded_sides++;
    }

    // Check top side: rect1's top edge is at or above rect2's top edge
    if (rect1.top <= rect2.top) {
        bounded_sides++;
    }

    // Check bottom side: rect1's bottom edge is at or below rect2's bottom edge
    if (rect1.top + rect1.height >= rect2.top + rect2.height) {
        bounded_sides++;
    }

    return bounded_sides;
}
