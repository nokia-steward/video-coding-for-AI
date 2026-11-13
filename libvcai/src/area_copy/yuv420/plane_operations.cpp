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
         * @brief Pure function to calculate plane offsets for YUV420 format.
         * @param width The width of the Y plane.
         * @param height The height of the Y plane.
         * @return A struct containing the calculated offsets.
         */
        struct yuv420_plane_offsets {
            size_t luma_size;
            size_t chroma_u_offset;
            size_t chroma_v_offset;
        };

        [[nodiscard]] constexpr yuv420_plane_offsets calculate_yuv420_plane_offsets(const int width, const int height) {
            const size_t luma_size = static_cast<size_t>(width * height);
            const size_t chroma_u_offset = luma_size;
            const size_t chroma_v_offset = luma_size + luma_size / 4;
            return {luma_size, chroma_u_offset, chroma_v_offset};
        }

        /**
         * @brief Validates copy region parameters.
         * @param width Width of region to copy.
         * @param height Height of region to copy.
         * @param src_x Source X coordinate.
         * @param src_y Source Y coordinate.
         * @param dst_x Destination X coordinate.
         * @param dst_y Destination Y coordinate.
         * @param src_stride Source stride.
         * @param src_plane_height Source plane height.
         * @param dst_stride Destination stride.
         * @param dst_plane_height Destination plane height.
         */
        void validate_copy_region(const int width, const int height, const int src_x, const int src_y, const int dst_x,
                                  const int dst_y, const int src_stride, const int src_plane_height,
                                  const int dst_stride, const int dst_plane_height) {
            if (width <= 0 || height <= 0) {
                debug(area_copy, "Copy region has zero or negative size. width=%d, height=%d. Skipping.", width,
                      height);
                return;
            }

            if (src_x < 0 || src_y < 0 || dst_x < 0 || dst_y < 0) {
                error(area_copy, "copy_plane_region: Negative coordinate or size provided.");
                throw std::out_of_range("Negative coordinate or size for copy_plane_region");
            }

            if (src_x + width > src_stride) {
                error(area_copy,
                      "copy_plane_region: Source read out of bounds (width). "
                      "src_x (%d) + width (%d) > src_stride (%d). "
                      "This indicates a packed rectangle exceeds the frame boundaries.",
                      src_x, width, src_stride);
                throw std::out_of_range("Source copy width exceeds stride");
            }
            if (src_y + height > src_plane_height) {
                error(area_copy,
                      "copy_plane_region: Source read out of bounds (height). "
                      "src_y (%d) + height (%d) > src_plane_height (%d)",
                      src_y, height, src_plane_height);
                throw std::out_of_range("Source copy height exceeds plane height");
            }

            if (dst_x + width > dst_stride) {
                error(area_copy,
                      "copy_plane_region: Destination write out of bounds (width). "
                      "dst_x (%d) + width (%d) > dst_stride (%d)",
                      dst_x, width, dst_stride);
                throw std::out_of_range("Destination copy width exceeds stride");
            }
            if (dst_y + height > dst_plane_height) {
                error(area_copy,
                      "copy_plane_region: Destination write out of bounds (height). "
                      "dst_y (%d) + height (%d) > dst_plane_height (%d)",
                      dst_y, height, dst_plane_height);
                throw std::out_of_range("Destination copy height exceeds plane height");
            }
        }

        /**
         * @brief Copies a single row of a plane region.
         * @param src_line Source line pointer.
         * @param dst_line Destination line pointer.
         * @param width Width in bytes to copy.
         */
        void copy_plane_row(const uint8_t *const src_line, uint8_t *const dst_line, const int width) {
            std::memcpy(dst_line, src_line, static_cast<size_t>(width));
        }
    } // namespace

    void copy_plane_region(const uint8_t *const src, uint8_t *const dst, const int src_stride, const int dst_stride,
                           const int src_plane_height, const int dst_plane_height, const int src_x, const int src_y,
                           const int dst_x, const int dst_y, const int width, const int height) {
        validate_copy_region(width, height, src_x, src_y, dst_x, dst_y, src_stride, src_plane_height, dst_stride,
                             dst_plane_height);

        if (width <= 0 || height <= 0) {
            return;
        }

        for (int row_index = 0; row_index < height; ++row_index) {
            const uint8_t *const src_line = src + (src_y + row_index) * src_stride + src_x;
            uint8_t *const dst_line = dst + (dst_y + row_index) * dst_stride + dst_x;
            copy_plane_row(src_line, dst_line, width);
        }
    }

    /**
     * @brief Calculates the required pointers for Y, U, and V planes from mapped buffers.
     * @param in_map_info Mapped input buffer.
     * @param out_map_info Mapped output buffer.
     * @param width The width of the Y plane.
     * @param height The height of the Y plane.
     * @return A struct containing all six plane pointers.
     */
    yuv_420_plane_pointers get_yuv420_plane_pointers(const GstMapInfo &in_map_info, const GstMapInfo &out_map_info,
                                                     const int width, const int height) {
        const auto offsets = calculate_yuv420_plane_offsets(width, height);

        const auto *const y_in = in_map_info.data;
        const auto *const u_in = y_in + offsets.chroma_u_offset;
        const auto *const v_in = y_in + offsets.chroma_v_offset;

        auto *const y_out = out_map_info.data;
        auto *const u_out = y_out + offsets.chroma_u_offset;
        auto *const v_out = y_out + offsets.chroma_v_offset;

        return {y_in, u_in, v_in, y_out, u_out, v_out};
    }
} // namespace sf
