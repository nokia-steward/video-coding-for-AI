// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../../libvcai/include/rectangle.h"

/**
 * @file validation.height
 * @brief Declares rectangle validation and adjustment functions.
 */

/**
 * @brief Ensures a rectangle has even dimensions and coordinates for YUV420 compatibility.
 * @param rect The rectangle to adjust
 * @param frame_width The width of the frame (for bounds checking)
 * @param frame_height The height of the frame (for bounds checking)
 * @return Rectangle with even dimensions and coordinates
 */
[[nodiscard]] sf::rectangle ensure_even_rectangle(const sf::rectangle &rect, int frame_width, int frame_height);
