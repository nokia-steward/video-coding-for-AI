// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"

#include <gst/gst.h>

// Debug category for area copy utilities.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Ensures even x coordinate with frame bounds checking.
         * @param rect Rectangle to modify.
         * @param frame_width Frame width for bounds checking.
         */
        void ensure_even_x(rectangle &rect, const int frame_width) {
            if (rect.left % 2 != 0) {
                if (rect.left + 1 < frame_width) {
                    rect.left += 1;
                } else {
                    rect.left -= 1;
                }
            }
        }

        /**
         * @brief Ensures even y coordinate with frame bounds checking.
         * @param rect Rectangle to modify.
         * @param frame_height Frame height for bounds checking.
         */
        void ensure_even_y(rectangle &rect, const int frame_height) {
            if (rect.top % 2 != 0) {
                if (rect.top + 1 < frame_height) {
                    rect.top += 1;
                } else {
                    rect.top -= 1;
                }
            }
        }

        /**
         * @brief Ensures even width by rounding down.
         * @param rect Rectangle to modify.
         */
        void ensure_even_width(rectangle &rect) {
            if (rect.width % 2 != 0) {
                rect.width -= 1;
            }
        }

        /**
         * @brief Ensures even height by rounding down.
         * @param rect Rectangle to modify.
         */
        void ensure_even_height(rectangle &rect) {
            if (rect.height % 2 != 0) {
                rect.height -= 1;
            }
        }

        /**
         * @brief Clamps rectangle width to fit within frame bounds.
         * @param rect Rectangle to modify.
         * @param frame_width Frame width.
         */
        void clamp_width_to_bounds(rectangle &rect, const int frame_width) {
            if (rect.left + rect.width > frame_width) {
                rect.width = frame_width - rect.left;
                if (rect.width % 2 != 0) {
                    rect.width -= 1;
                }
            }
        }

        /**
         * @brief Clamps rectangle height to fit within frame bounds.
         * @param rect Rectangle to modify.
         * @param frame_height Frame height.
         */
        void clamp_height_to_bounds(rectangle &rect, const int frame_height) {
            if (rect.top + rect.height > frame_height) {
                rect.height = frame_height - rect.top;
                if (rect.height % 2 != 0) {
                    rect.height -= 1;
                }
            }
        }

        /**
         * @brief Ensures minimum rectangle size (2x2).
         * @param rect Rectangle to modify.
         */
        void ensure_minimum_size(rectangle &rect) {
            if (rect.width < 2) {
                rect.width = 2;
            }
            if (rect.height < 2) {
                rect.height = 2;
            }
        }
    } // namespace

    [[nodiscard]] rectangle ensure_even_dimensions(const rectangle &rect, const int frame_width,
                                                   const int frame_height) {
        rectangle corrected = rect;

        ensure_even_x(corrected, frame_width);
        ensure_even_y(corrected, frame_height);
        ensure_even_width(corrected);
        ensure_even_height(corrected);
        clamp_width_to_bounds(corrected, frame_width);
        clamp_height_to_bounds(corrected, frame_height);
        ensure_minimum_size(corrected);

        return corrected;
    }

    [[nodiscard]] rectangle ensure_even_dimensions(const rectangle &rect) {
        rectangle corrected = rect;

        if (corrected.left % 2 != 0) {
            corrected.left += 1;
        }
        if (corrected.top % 2 != 0) {
            corrected.top += 1;
        }
        ensure_even_width(corrected);
        ensure_even_height(corrected);
        ensure_minimum_size(corrected);

        return corrected;
    }

    [[nodiscard]] bool has_even_dimensions(const rectangle &rect) {
        return rect.width % 2 == 0 && rect.height % 2 == 0 && rect.left % 2 == 0 && rect.top % 2 == 0;
    }
} // namespace sf
