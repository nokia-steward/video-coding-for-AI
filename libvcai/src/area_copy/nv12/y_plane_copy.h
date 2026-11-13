// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Copies Y plane for a single NV12 ROI.
     * @param y_in Input Y plane pointer.
     * @param y_out Output Y plane pointer.
     * @param y_stride_in Input Y stride.
     * @param luma_stride_out Output Y stride.
     * @param input_rect_even Sanitized input rectangle.
     * @param clamped_packed_x Clamped packed X position.
     * @param clamped_packed_y Clamped packed Y position.
     * @param copy_w_even Even-aligned copy width.
     * @param copy_h_even Even-aligned copy height.
     * @param rect_index Index for logging.
     */
    void copy_nv12_y_plane(const uint8_t *const y_in, uint8_t *const y_out, const int y_stride_in,
                           const int luma_stride_out, const rectangle &input_rect_even, const int clamped_packed_x,
                           const int clamped_packed_y, const int copy_w_even, const int copy_h_even,
                           const size_t rect_index);
} // namespace sf
