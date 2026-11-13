// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Copies UV plane for a single NV12 ROI.
     * @param uv_in Input UV plane pointer.
     * @param uv_out Output UV plane pointer.
     * @param uv_stride_in Input UV stride.
     * @param uv_stride_out Output UV stride.
     * @param uv_plane_height_out Output UV plane height (for bounds validation).
     * @param input_rect_even Sanitized input rectangle.
     * @param clamped_packed_x Clamped packed X position.
     * @param clamped_packed_y Clamped packed Y position.
     * @param copy_w_even Even-aligned copy width.
     * @param copy_h_even Even-aligned copy height.
     * @param rect_index Index for logging.
     */
    void copy_nv12_uv_plane(const uint8_t *const uv_in, uint8_t *const uv_out, const int uv_stride_in,
                            const int uv_stride_out, const int uv_plane_height_out, const rectangle &input_rect_even,
                            const int clamped_packed_x, const int clamped_packed_y, const int copy_w_even,
                            const int copy_h_even, const size_t rect_index);
} // namespace sf
