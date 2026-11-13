// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "validation.h"
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file validation.cpp
 * @brief Implementation of rectangle validation and adjustment functions.
 */

/**
 * @brief Ensures a rectangle has even coordinates and dimensions for video encoding compatibility.
 * @param rect Rectangle to adjust.
 * @param frame_width Frame width for boundary checking.
 * @param frame_height Frame height for boundary checking.
 * @return Adjusted rectangle with even coordinates and dimensions.
 */
sf::rectangle ensure_even_rectangle(const sf::rectangle &rect, const int frame_width, const int frame_height) {
    sf::rectangle adjusted = rect;

    // Ensure even x coordinate by rounding down (previous even)
    adjusted.left = (adjusted.left / 2) * 2;

    // Ensure even y coordinate by rounding down (previous even)
    adjusted.top = (adjusted.top / 2) * 2;

    // Ensure even width by rounding up (next even)
    adjusted.width = ((adjusted.width + 1) / 2) * 2;

    // Ensure even height by rounding up (next even)
    adjusted.height = ((adjusted.height + 1) / 2) * 2;

    // Ensure the rectangle still fits within frame bounds
    if (adjusted.left + adjusted.width > frame_width) {
        adjusted.width = frame_width - adjusted.left;
        // Ensure width is still even after bounds adjustment
        adjusted.width = (adjusted.width / 2) * 2;
    }

    if (adjusted.top + adjusted.height > frame_height) {
        adjusted.height = frame_height - adjusted.top;
        // Ensure height is still even after bounds adjustment
        adjusted.height = (adjusted.height / 2) * 2;
    }

    // Ensure minimum size (at least 2x2 for even dimensions)
    if (adjusted.width < 2) {
        adjusted.width = 2;
    }
    if (adjusted.height < 2) {
        adjusted.height = 2;
    }

    return adjusted;
}
