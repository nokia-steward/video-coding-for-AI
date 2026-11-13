// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/rectangle.h"
#include "../clamped_copy_region.h"
#include "plane_operations.h"

#include <algorithm>
#include <vector>

namespace sf {
    namespace {
        /**
         * @brief Copies Y plane for a single rectangle.
         * @param planes Plane pointers.
         * @param luma_stride Y plane stride.
         * @param luma_plane_height Y plane height.
         * @param input_rect Input rectangle.
         * @param packed_rect Packed rectangle.
         */
        void copy_y_plane_for_rect(const yuv_420_plane_pointers &planes, const int luma_stride,
                                   const int luma_plane_height, const rectangle &input_rect,
                                   const rectangle &packed_rect) {
            const auto region = area_copy_internal::clamp_copy_region(
                    input_rect.left, input_rect.top, packed_rect.left, packed_rect.top, input_rect.width,
                    input_rect.height, luma_stride, luma_plane_height, luma_stride, luma_plane_height);

            if (!region.is_valid()) {
                return;
            }

            copy_plane_region(planes.y_in, planes.y_out, luma_stride, luma_stride, luma_plane_height, luma_plane_height,
                              region.src_x, region.src_y, region.dst_x, region.dst_y, region.width, region.height);
        }

        /**
         * @brief Copies UV planes for a single rectangle.
         * @param planes Plane pointers.
         * @param uv_stride UV plane stride.
         * @param uv_plane_height UV plane height.
         * @param input_rect Input rectangle.
         * @param packed_rect Packed rectangle.
         */
        void copy_uv_planes_for_rect(const yuv_420_plane_pointers &planes, const int uv_stride,
                                     const int uv_plane_height, const rectangle &input_rect,
                                     const rectangle &packed_rect) {
            const int src_uv_x = input_rect.left / 2;
            const int src_uv_y = input_rect.top / 2;
            const int dst_uv_x = packed_rect.left / 2;
            const int dst_uv_y = packed_rect.top / 2;
            // Use input_rect dimensions for copy size (what we're copying), packed_rect only tells us where to place it
            const int uv_w = input_rect.width / 2;
            const int uv_h = input_rect.height / 2;

            const auto region =
                    area_copy_internal::clamp_copy_region(src_uv_x, src_uv_y, dst_uv_x, dst_uv_y, uv_w, uv_h, uv_stride,
                                                          uv_plane_height, uv_stride, uv_plane_height);
            if (!region.is_valid()) {
                return;
            }

            copy_plane_region(planes.u_in, planes.u_out, uv_stride, uv_stride, uv_plane_height, uv_plane_height,
                              region.src_x, region.src_y, region.dst_x, region.dst_y, region.width, region.height);
            copy_plane_region(planes.v_in, planes.v_out, uv_stride, uv_stride, uv_plane_height, uv_plane_height,
                              region.src_x, region.src_y, region.dst_x, region.dst_y, region.width, region.height);
        }
    } // namespace

    /**
     * @brief Processes a YUV420 frame by copying specified regions.
     * @param in_map_info Mapped input buffer.
     * @param out_map_info Mapped output buffer.
     * @param input_width Input frame width.
     * @param input_height Input frame height.
     * @param input_rectangles Input rectangles.
     * @param packed_rectangles Packed rectangles.
     */
    void process_yuv420_frame(const GstMapInfo &in_map_info, const GstMapInfo &out_map_info, const int input_width,
                              const int input_height, const std::vector<rectangle> &input_rectangles,
                              const std::vector<rectangle> &packed_rectangles) {
        const auto planes = get_yuv420_plane_pointers(in_map_info, out_map_info, input_width, input_height);
        const int luma_stride = input_width;
        const int uv_stride = input_width / 2;
        const int luma_plane_height = input_height;
        const int uv_plane_height = input_height / 2;

        for (size_t rect_index = 0; rect_index < packed_rectangles.size(); ++rect_index) {
            if (rect_index >= input_rectangles.size()) {
                break;
            }
            const auto &packed_rect = packed_rectangles[rect_index];
            const auto &input_rect = input_rectangles[rect_index];

            copy_y_plane_for_rect(planes, luma_stride, luma_plane_height, input_rect, packed_rect);
            copy_uv_planes_for_rect(planes, uv_stride, uv_plane_height, input_rect, packed_rect);
        }
    }
} // namespace sf
