// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"
#include "plane_operations.h"
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
         * @brief Samples UV values for verification (first ROI only).
         * @param uv_in Input UV plane pointer.
         * @param uv_stride_in Input UV stride.
         * @param input_rect_even Sanitized input rectangle.
         * @param rect_index Index for logging.
         */
        void sample_uv_values(const uint8_t *const uv_in, const int uv_stride_in, const rectangle &input_rect_even,
                              const size_t rect_index, const int uv_width_bytes) {
            if (rect_index == 0 && uv_width_bytes >= 2) {
                const int src_uv_offset = (input_rect_even.top / 2) * uv_stride_in + input_rect_even.left;
                const uint8_t sample_u = uv_in[src_uv_offset];
                const uint8_t sample_v = uv_in[src_uv_offset + 1];
                info(area_copy, "ROI %zu: Sample UV values from input buffer: U=%d, V=%d (at offset %d)", rect_index,
                     sample_u, sample_v, src_uv_offset);
            }
        }

        /**
         * @brief Verifies copied UV values (first ROI only).
         * @param uv_in Input UV plane pointer.
         * @param uv_out Output UV plane pointer.
         * @param uv_stride_in Input UV stride.
         * @param uv_stride_out Output UV stride.
         * @param input_rect_even Sanitized input rectangle.
         * @param clamped_packed_x Clamped packed X position.
         * @param clamped_packed_y Clamped packed Y position.
         * @param uv_width_bytes UV width in bytes.
         * @param uv_height UV height in rows.
         * @param rect_index Index for logging.
         */
        void verify_copied_uv_values(const uint8_t *const uv_in, const uint8_t *const uv_out, const int uv_stride_in,
                                     const int uv_stride_out, const rectangle &input_rect_even,
                                     const int clamped_packed_x, const int clamped_packed_y, const int uv_width_bytes,
                                     const int uv_height, const size_t rect_index) {
            if (rect_index == 0 && uv_height > 0 && uv_width_bytes >= 2) {
                const int dst_uv_offset = (clamped_packed_y / 2) * uv_stride_out + clamped_packed_x;
                const uint8_t copied_u = uv_out[dst_uv_offset];
                const uint8_t copied_v = uv_out[dst_uv_offset + 1];
                info(area_copy, "ROI %zu: Copied UV values to output buffer: U=%d, V=%d (at offset %d)", rect_index,
                     copied_u, copied_v, dst_uv_offset);

                if (uv_width_bytes >= 4 && uv_height >= 2) {
                    const int mid_uv_offset = ((clamped_packed_y / 2) + (uv_height / 2)) * uv_stride_out +
                                              clamped_packed_x + (uv_width_bytes / 2);
                    const uint8_t mid_u = uv_out[mid_uv_offset];
                    const uint8_t mid_v = uv_out[mid_uv_offset + 1];
                    const int src_mid_uv_offset = ((input_rect_even.top / 2) + (uv_height / 2)) * uv_stride_in +
                                                  input_rect_even.left + (uv_width_bytes / 2);
                    const uint8_t src_mid_u = uv_in[src_mid_uv_offset];
                    const uint8_t src_mid_v = uv_in[src_mid_uv_offset + 1];
                    info(area_copy, "ROI %zu: Middle UV - input: U=%d, V=%d -> output: U=%d, V=%d", rect_index,
                         src_mid_u, src_mid_v, mid_u, mid_v);
                }
            }
        }
    } // namespace

    /**
     * @brief Copies UV plane for a single NV12 ROI.
     * @param uv_in Input UV plane pointer.
     * @param uv_out Output UV plane pointer.
     * @param uv_stride_in Input UV stride.
     * @param uv_stride_out Output UV stride.
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
                            const int copy_h_even, const size_t rect_index) {
        const int uv_width_bytes = copy_w_even;
        const int uv_height = copy_h_even / 2;

        if (uv_width_bytes <= 0 || uv_height <= 0) {
            warning(area_copy, "ROI %zu: UV copy dimensions invalid (width_bytes: %d, height: %d)", rect_index,
                    uv_width_bytes, uv_height);
            return;
        }

        // Validate bounds before copying to prevent buffer overruns
        // For NV12, clamped_packed_x is in pixels, but UV plane uses byte offsets
        // Each pixel in UV plane is 2 bytes (interleaved U,V), so byte offset equals pixel offset
        const int dst_x_bytes = clamped_packed_x;
        const int dst_y = clamped_packed_y / 2;
        const int src_x_bytes = input_rect_even.left;
        const int src_y = input_rect_even.top / 2;

        // Validate source bounds
        if (src_x_bytes + uv_width_bytes > uv_stride_in) {
            error(area_copy, "ROI %zu: Source UV copy out of bounds (src_x_bytes=%d + width_bytes=%d > stride_in=%d)",
                  rect_index, src_x_bytes, uv_width_bytes, uv_stride_in);
            return;
        }

        // Validate destination bounds - CRITICAL to prevent buffer overruns
        if (dst_x_bytes + uv_width_bytes > uv_stride_out) {
            error(area_copy,
                  "ROI %zu: Destination UV copy out of bounds (dst_x_bytes=%d + width_bytes=%d > stride_out=%d). "
                  "This would cause buffer overrun!",
                  rect_index, dst_x_bytes, uv_width_bytes, uv_stride_out);
            return;
        }
        if (dst_y + uv_height > uv_plane_height_out) {
            error(area_copy,
                  "ROI %zu: Destination UV copy out of bounds (dst_y=%d + height=%d > plane_height=%d). "
                  "This would cause buffer overrun!",
                  rect_index, dst_y, uv_height, uv_plane_height_out);
            return;
        }

        debug(area_copy, "Copying UV for ROI %zu: src (%d,%d) %dx%d -> dst (%d,%d) (stride_in: %d, stride_out: %d)",
              rect_index, input_rect_even.left, input_rect_even.top / 2, uv_width_bytes, uv_height, clamped_packed_x,
              clamped_packed_y / 2, uv_stride_in, uv_stride_out);

        sample_uv_values(uv_in, uv_stride_in, input_rect_even, rect_index, uv_width_bytes);

        for (int row = 0; row < uv_height; ++row) {
            const int dst_offset = (dst_y + row) * uv_stride_out + dst_x_bytes;
            const int src_offset = (src_y + row) * uv_stride_in + src_x_bytes;

            std::memcpy(uv_out + dst_offset, uv_in + src_offset, static_cast<size_t>(uv_width_bytes));
        }

        verify_copied_uv_values(uv_in, uv_out, uv_stride_in, uv_stride_out, input_rect_even, clamped_packed_x,
                                clamped_packed_y, uv_width_bytes, uv_height, rect_index);
    }
} // namespace sf
