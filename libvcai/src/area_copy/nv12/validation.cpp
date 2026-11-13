// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "validation.h"
#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"

#include <algorithm>
#include <gst/video/video-info.h>
#include <gst/video/video.h>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Sanitizes rectangle coordinates to ensure even alignment for NV12.
         * @param rect Rectangle to sanitize.
         * @return Sanitized rectangle with even coordinates and dimensions.
         */
        [[nodiscard]] rectangle sanitize_rectangle_for_nv12(const rectangle &rect) {
            rectangle sanitized = rect;
            sanitized.left &= ~1;
            sanitized.top &= ~1;
            sanitized.width &= ~1;
            sanitized.height &= ~1;
            return sanitized;
        }

        /**
         * @brief Validates rectangle dimensions after sanitization.
         * @param rect Rectangle to validate.
         * @param rect_index Index for logging.
         * @return true if valid, false otherwise.
         */
        [[nodiscard]] bool validate_sanitized_rectangle(const rectangle &rect, const size_t rect_index) {
            if (rect.width <= 0 || rect.height <= 0) {
                warning(area_copy, "ROI %zu has invalid dimensions after sanitization (%dx%d), skipping", rect_index,
                        rect.width, rect.height);
                return false;
            }
            return true;
        }

        /**
         * @brief Clamps packed position to fit within output buffer.
         * @param packed_x Packed X coordinate.
         * @param packed_y Packed Y coordinate.
         * @param copy_w Copy width.
         * @param copy_h Copy height.
         * @param max_output_x Maximum output X.
         * @param max_output_y Maximum output Y.
         * @return Clamped coordinates (x, y).
         */
        [[nodiscard]] std::pair<int, int> clamp_packed_position(const int packed_x, const int packed_y,
                                                                const int copy_w, const int copy_h,
                                                                const int max_output_x, const int max_output_y) {
            const int clamped_x = std::max(0, std::min(packed_x, max_output_x - copy_w));
            const int clamped_y = std::max(0, std::min(packed_y, max_output_y - copy_h));
            return {clamped_x, clamped_y};
        }

        /**
         * @brief Calculates actual copy dimensions considering bounds.
         * @param input_w Input width.
         * @param input_h Input height.
         * @param clamped_x Clamped X position.
         * @param clamped_y Clamped Y position.
         * @param max_output_x Maximum output X.
         * @param max_output_y Maximum output Y.
         * @return Copy dimensions (width, height).
         */
        [[nodiscard]] std::pair<int, int> calculate_copy_dimensions(const int input_w, const int input_h,
                                                                    const int clamped_x, const int clamped_y,
                                                                    const int max_output_x, const int max_output_y) {
            const int copy_w = std::min(input_w, max_output_x - clamped_x);
            const int copy_h = std::min(input_h, max_output_y - clamped_y);
            return {copy_w, copy_h};
        }

        /**
         * @brief Ensures copy dimensions are even.
         * @param copy_w Copy width.
         * @param copy_h Copy height.
         * @return Even-aligned dimensions (width, height).
         */
        [[nodiscard]] std::pair<int, int> ensure_even_copy_dimensions(const int copy_w, const int copy_h) {
            const int copy_w_even = copy_w & ~1;
            const int copy_h_even = copy_h & ~1;
            return {copy_w_even, copy_h_even};
        }

        /**
         * @brief Validates copy dimensions after even alignment.
         * @param copy_w_even Even-aligned copy width.
         * @param copy_h_even Even-aligned copy height.
         * @param rect_index Index for logging.
         * @return true if valid, false otherwise.
         */
        [[nodiscard]] bool validate_copy_dimensions(const int copy_w_even, const int copy_h_even,
                                                    const size_t rect_index) {
            if (copy_w_even <= 0 || copy_h_even <= 0) {
                warning(area_copy, "ROI %zu copy dimensions invalid after even alignment (%dx%d), skipping", rect_index,
                        copy_w_even, copy_h_even);
                return false;
            }
            return true;
        }
    } // namespace

    /**
     * @brief Sanitizes input rectangle for NV12 processing.
     * @param input_rect Input rectangle.
     * @return Sanitized rectangle.
     */
    [[nodiscard]] rectangle sanitize_input_rectangle_nv12(const rectangle &input_rect) {
        return sanitize_rectangle_for_nv12(input_rect);
    }

    /**
     * @brief Sanitizes packed rectangle for NV12 processing.
     * @param packed_rect Packed rectangle.
     * @return Sanitized rectangle (position only, dimensions preserved from input).
     */
    [[nodiscard]] rectangle sanitize_packed_rectangle_nv12(const rectangle &packed_rect) {
        rectangle sanitized = packed_rect;
        sanitized.left &= ~1;
        sanitized.top &= ~1;
        return sanitized;
    }

    /**
     * @brief Validates and calculates copy parameters for NV12 ROI.
     * @param input_rect_even Sanitized input rectangle.
     * @param packed_rect_even Sanitized packed rectangle.
     * @param max_output_x Maximum output X.
     * @param max_output_y Maximum output Y.
     * @param rect_index Index for logging.
     * @return Optional tuple of (clamped_x, clamped_y, copy_w_even, copy_h_even) if valid.
     */
    [[nodiscard]] std::optional<std::tuple<int, int, int, int>>
    validate_and_calculate_nv12_copy_params(const rectangle &input_rect_even, const rectangle &packed_rect_even,
                                            const int max_output_x, const int max_output_y, const size_t rect_index) {
        if (!validate_sanitized_rectangle(input_rect_even, rect_index)) {
            return std::nullopt;
        }

        const auto [clamped_x, clamped_y] =
                clamp_packed_position(packed_rect_even.left, packed_rect_even.top, input_rect_even.width,
                                      input_rect_even.height, max_output_x, max_output_y);

        const auto [copy_w, copy_h] = calculate_copy_dimensions(input_rect_even.width, input_rect_even.height,
                                                                clamped_x, clamped_y, max_output_x, max_output_y);

        if (copy_w <= 0 || copy_h <= 0) {
            warning(area_copy,
                    "ROI %zu would be out of bounds (input: %dx%d at (%d,%d), packed pos: (%d,%d), output: %dx%d), "
                    "skipping",
                    rect_index, input_rect_even.width, input_rect_even.height, input_rect_even.left,
                    input_rect_even.top, clamped_x, clamped_y, max_output_x, max_output_y);
            return std::nullopt;
        }

        const auto [copy_w_even, copy_h_even] = ensure_even_copy_dimensions(copy_w, copy_h);

        if (!validate_copy_dimensions(copy_w_even, copy_h_even, rect_index)) {
            return std::nullopt;
        }

        return std::make_tuple(clamped_x, clamped_y, copy_w_even, copy_h_even);
    }
} // namespace sf
