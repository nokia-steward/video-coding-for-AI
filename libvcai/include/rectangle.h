// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <algorithm>

namespace sf {
    /**
     * @brief Simple rectangle structure with position and dimensions.
     *
     * This replaces the rectpack2D dependency with our own lightweight implementation.
     *
     * This struct follows functional programming principles:
     * - Immutable by default: all member functions are const
     * - Pure functions: all methods are side-effect free
     * - Value semantics: designed for easy copying and passing by value
     * - No hidden state: all data is public and accessible
     */
    struct rectangle {
        int left; // X coordinate (left edge)
        int top; // Y coordinate (top edge)
        int width; // Width
        int height; // Height

        /**
         * @brief Constructs an empty rectangle at the origin.
         */
        rectangle() : left(0), top(0), width(0), height(0) {}

        /**
         * @brief Constructs a rectangle with position and dimensions.
         * @param x_pos Left edge position.
         * @param y_pos Top edge position.
         * @param width Rectangle width.
         * @param height Rectangle height.
         */
        rectangle(const int x_pos, const int y_pos, const int rect_width, const int rect_height) :
            left(x_pos), top(y_pos), width(rect_width), height(rect_height) {}

        /**
         * @brief Copy-constructs a rectangle.
         * @param other Source rectangle.
         */
        rectangle(const rectangle &other) = default;

        /**
         * @brief Copy-assigns a rectangle.
         * @param other Source rectangle.
         * @return Reference to this rectangle.
         */
        rectangle &operator=(const rectangle &other) = default;

        /**
         * @brief Compute rectangle area.
         * @return Area in square pixels.
         */
        [[nodiscard]] int area() const { return width * height; }

        /**
         * @brief Compute rectangle perimeter.
         * @return Perimeter in pixels.
         */
        [[nodiscard]] int perimeter() const { return 2 * width + 2 * height; }

        /**
         * @brief Get the larger of width and height.
         * @return Maximum side length.
         */
        [[nodiscard]] int max_side() const { return std::max(width, height); }

        /**
         * @brief Get the smaller of width and height.
         * @return Minimum side length.
         */
        [[nodiscard]] int min_side() const { return std::min(width, height); }

        /**
         * @brief Check if rectangle dimensions are positive.
         * @return true when width and height are positive.
         */
        [[nodiscard]] bool is_valid() const { return width > 0 && height > 0; }

        /**
         * @brief Check whether a point lies inside the rectangle.
         * @param px X coordinate of the point.
         * @param py Y coordinate of the point.
         * @return true if the point is inside the rectangle.
         */
        [[nodiscard]] bool contains(const int px, const int py) const {
            return px >= left && px < left + width && py >= top && py < top + height;
        }

        /**
         * @brief Check whether this rectangle intersects another.
         * @param other Rectangle to test against.
         * @return true if rectangles overlap.
         */
        [[nodiscard]] bool intersects(const rectangle &other) const {
            return !(left >= other.left + other.width || left + width <= other.left ||
                     top >= other.top + other.height || top + height <= other.top);
        }

        /**
         * @brief Compute the intersection of this rectangle and another.
         * @param other Rectangle to intersect with.
         * @return Intersection rectangle (empty if no overlap).
         */
        [[nodiscard]] rectangle intersection(const rectangle &other) const {
            const int intersection_left = std::max(left, other.left);
            const int intersection_top = std::max(top, other.top);
            const int right = std::min(left + width, other.left + other.width);
            const int bottom = std::min(top + height, other.top + other.height);

            if (intersection_left < right && intersection_top < bottom) {
                return rectangle{intersection_left, intersection_top, right - intersection_left,
                                 bottom - intersection_top};
            }
            return rectangle{0, 0, 0, 0};
        }

        /**
         * @brief Expand this rectangle to include another rectangle.
         * @param other Rectangle to include.
         */
        void expand_with(const rectangle &other) {
            if (other.width == 0 || other.height == 0)
                return; // Skip empty rectangles

            const int new_left = std::min(left, other.left);
            const int new_top = std::min(top, other.top);
            const int new_width = std::max(left + width, other.left + other.width) - new_left;
            const int new_height = std::max(top + height, other.top + other.height) - new_top;

            left = new_left;
            top = new_top;
            width = new_width;
            height = new_height;
        }

        /**
         * @brief Calculate intersection-over-union with another rectangle.
         * @param other Rectangle to compare against.
         * @return IOU ratio in range [0.0, 1.0].
         */
        [[nodiscard]] double calculate_iou(const rectangle &other) const {
            const auto inter = intersection(other);
            const int inter_area = inter.area();

            if (inter_area == 0) {
                return 0.0;
            }

            const int area1 = area();
            const int area2 = other.area();
            const int union_area = area1 + area2 - inter_area;

            if (union_area == 0) {
                return 0.0;
            }

            return static_cast<double>(inter_area) / static_cast<double>(union_area);
        }

        /**
         * @brief Compare rectangles for equality.
         * @param other Rectangle to compare against.
         * @return true if all fields match.
         */
        bool operator==(const rectangle &other) const {
            return left == other.left && top == other.top && width == other.width && height == other.height;
        }

        /**
         * @brief Compare rectangles for inequality.
         * @param other Rectangle to compare against.
         * @return true if any field differs.
         */
        bool operator!=(const rectangle &other) const { return !(*this == other); }
    };

} // namespace sf
