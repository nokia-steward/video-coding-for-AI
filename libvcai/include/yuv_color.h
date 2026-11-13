// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

/**
 * @file yuv_color.h
 * @brief Defines YUV color enum for drawing borders and text overlays.
 */

namespace sf {
    /**
     * @enum yuv_color
     * @brief Defines the available YUV colors for drawing borders and text overlays.
     */
    enum struct yuv_color {
        blue, ///< Blue color for areas detected by the inference engine
        purple, ///< Purple color for areas detected with low confidence
        red, ///< Red color for areas still being tracked but no longer detected
        yellow_orange, ///< Yellowish-orange color for merged ROIs with mixed prediction status
        white, ///< White color for text overlays
        dark_gray ///< Dark gray color for text backgrounds
    };
} // namespace sf
