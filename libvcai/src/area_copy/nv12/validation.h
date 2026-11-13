// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <tuple>
#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Sanitizes input rectangle for NV12 processing.
     * @param input_rect Input rectangle.
     * @return Sanitized rectangle.
     */
    [[nodiscard]] rectangle sanitize_input_rectangle_nv12(const rectangle &input_rect);

    /**
     * @brief Sanitizes packed rectangle for NV12 processing.
     * @param packed_rect Packed rectangle.
     * @return Sanitized rectangle (position only, dimensions preserved from input).
     */
    [[nodiscard]] rectangle sanitize_packed_rectangle_nv12(const rectangle &packed_rect);

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
                                            const int max_output_x, const int max_output_y, const size_t rect_index);
} // namespace sf
