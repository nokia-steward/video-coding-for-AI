// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"
#include "validation.h"

#include <cstring>
#include <gst/video/video-info.h>
#include <gst/video/video.h>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Copies Y plane rows for a single ROI.
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
        void copy_y_plane_rows(const uint8_t *const y_in, uint8_t *const y_out, const int y_stride_in,
                               const int luma_stride_out, const rectangle &input_rect_even, const int clamped_packed_x,
                               const int clamped_packed_y, const int copy_w_even, const int copy_h_even,
                               const size_t rect_index) {
            if (rect_index == 0 && copy_h_even > 0 && copy_w_even > 0) {
                const int src_y_offset = input_rect_even.top * y_stride_in + input_rect_even.left;
                const uint8_t sample_y = y_in[src_y_offset];
                info(area_copy, "ROI %zu: Sample Y value from input buffer: Y=%d (at offset %d)", rect_index, sample_y,
                     src_y_offset);
            }

            for (int row = 0; row < copy_h_even; ++row) {
                std::memcpy(y_out + ((clamped_packed_y + row) * luma_stride_out) + clamped_packed_x,
                            y_in + ((input_rect_even.top + row) * y_stride_in) + input_rect_even.left,
                            static_cast<size_t>(copy_w_even));
            }

            if (rect_index == 0 && copy_h_even > 0 && copy_w_even > 0) {
                const int dst_y_offset = clamped_packed_y * luma_stride_out + clamped_packed_x;
                const uint8_t copied_y = y_out[dst_y_offset];
                info(area_copy, "ROI %zu: Copied Y value to output buffer: Y=%d (at offset %d)", rect_index, copied_y,
                     dst_y_offset);
            }
        }
    } // namespace

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
                           const size_t rect_index) {
        copy_y_plane_rows(y_in, y_out, y_stride_in, luma_stride_out, input_rect_even, clamped_packed_x,
                          clamped_packed_y, copy_w_even, copy_h_even, rect_index);
    }
} // namespace sf
