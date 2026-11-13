// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"

#include <cstring>
#include <gst/gst.h>
#include <stdexcept>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Validates interleaved UV region copy parameters.
         * @param width_bytes Width in bytes.
         * @param height Height in rows.
         * @param src_x_bytes Source X in bytes.
         * @param src_y Source Y in rows.
         * @param dst_x_bytes Destination X in bytes.
         * @param dst_y Destination Y in rows.
         * @param src_stride_bytes Source stride in bytes.
         * @param src_plane_height Source plane height.
         * @param dst_stride_bytes Destination stride in bytes.
         * @param dst_plane_height Destination plane height.
         */
        void validate_interleaved_uv_region(const int width_bytes, const int height, const int src_x_bytes,
                                            const int src_y, const int dst_x_bytes, const int dst_y,
                                            const int src_stride_bytes, const int src_plane_height,
                                            const int dst_stride_bytes, const int dst_plane_height) {
            if (width_bytes <= 0 || height <= 0) {
                debug(area_copy,
                      "Copy interleaved UV region has zero or negative size. width_bytes=%d, height=%d. Skipping.",
                      width_bytes, height);
                return;
            }

            if (src_x_bytes < 0 || src_y < 0 || dst_x_bytes < 0 || dst_y < 0) {
                error(area_copy, "copy_interleaved_uv_region: Negative coordinate provided.");
                throw std::out_of_range("Negative coordinate for copy_interleaved_uv_region");
            }

            if (src_x_bytes + width_bytes > src_stride_bytes) {
                error(area_copy,
                      "copy_interleaved_uv_region: Source read out of bounds (width). "
                      "src_x_bytes (%d) + width_bytes (%d) > src_stride_bytes (%d)",
                      src_x_bytes, width_bytes, src_stride_bytes);
                throw std::out_of_range("Source copy width exceeds UV stride");
            }
            if (src_y + height > src_plane_height) {
                error(area_copy,
                      "copy_interleaved_uv_region: Source read out of bounds (height). "
                      "src_y (%d) + height (%d) > src_plane_height (%d)",
                      src_y, height, src_plane_height);
                throw std::out_of_range("Source copy height exceeds UV plane height");
            }

            if (dst_x_bytes + width_bytes > dst_stride_bytes) {
                error(area_copy,
                      "copy_interleaved_uv_region: Destination write out of bounds (width). "
                      "dst_x_bytes (%d) + width_bytes (%d) > dst_stride_bytes (%d)",
                      dst_x_bytes, width_bytes, dst_stride_bytes);
                throw std::out_of_range("Destination copy width exceeds UV stride");
            }
            if (dst_y + height > dst_plane_height) {
                error(area_copy,
                      "copy_interleaved_uv_region: Destination write out of bounds (height). "
                      "dst_y (%d) + height (%d) > dst_plane_height (%d)",
                      dst_y, height, dst_plane_height);
                throw std::out_of_range("Destination copy height exceeds UV plane height");
            }
        }

        /**
         * @brief Copies a single row of interleaved UV data.
         * @param src_line Source line pointer.
         * @param dst_line Destination line pointer.
         * @param width_bytes Width in bytes.
         */
        void copy_interleaved_uv_row(const uint8_t *const src_line, uint8_t *const dst_line, const int width_bytes) {
            std::memcpy(dst_line, src_line, static_cast<size_t>(width_bytes));
        }
    } // namespace

    /**
     * @brief Copies an interleaved UV region from NV12 format.
     * @param src_uv Pointer to the start of the source UV plane (interleaved U,V,U,V...).
     * @param dst_uv Pointer to the start of the destination UV plane (interleaved U,V,U,V...).
     * @param src_stride_bytes The stride of the source UV plane in bytes.
     * @param dst_stride_bytes The stride of the destination UV plane in bytes.
     * @param src_plane_height The total height of the source UV plane.
     * @param dst_plane_height The total height of the destination UV plane.
     * @param src_x_bytes The starting X coordinate in the source in BYTES (not pixels).
     * @param src_y The starting Y coordinate in the source (in UV plane rows).
     * @param dst_x_bytes The starting X coordinate in the destination in BYTES (not pixels).
     * @param dst_y The starting Y coordinate in the destination (in UV plane rows).
     * @param width_bytes The width of the region to copy in BYTES (not pixels).
     * @param height The height of the region to copy (in UV plane rows).
     *
     * Note: For NV12, X coordinates and width are in bytes because UV is interleaved.
     * If you have a pixel X coordinate 'x', the byte offset is 'x' (not x/2 or x*2).
     * This is because: (x/2 pixels) * (2 bytes per pixel) = x bytes.
     */
    void copy_interleaved_uv_region(const uint8_t *const src_uv, uint8_t *const dst_uv, const int src_stride_bytes,
                                    const int dst_stride_bytes, const int src_plane_height, const int dst_plane_height,
                                    const int src_x_bytes, const int src_y, const int dst_x_bytes, const int dst_y,
                                    const int width_bytes, const int height) {
        validate_interleaved_uv_region(width_bytes, height, src_x_bytes, src_y, dst_x_bytes, dst_y, src_stride_bytes,
                                       src_plane_height, dst_stride_bytes, dst_plane_height);

        if (width_bytes <= 0 || height <= 0) {
            return;
        }

        for (int row_index = 0; row_index < height; ++row_index) {
            const uint8_t *const src_line = src_uv + (src_y + row_index) * src_stride_bytes + src_x_bytes;
            uint8_t *const dst_line = dst_uv + (dst_y + row_index) * dst_stride_bytes + dst_x_bytes;
            copy_interleaved_uv_row(src_line, dst_line, width_bytes);
        }
    }
} // namespace sf
