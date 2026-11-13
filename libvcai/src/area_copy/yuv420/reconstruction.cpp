// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"
#include "../clamped_copy_region.h"
#include "plane_operations.h"

#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <vector>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Gets plane pointers for YUV420 reconstruction.
         * @param packed_map_info Packed frame map info.
         * @param reconstructed_map_info Reconstructed frame map info.
         * @param packed_info Packed frame video info.
         * @param reconstructed_info Reconstructed frame video info.
         * @return Plane pointers structure.
         */
        struct yuv420_reconstruction_planes {
            const uint8_t *y_in;
            const uint8_t *u_in;
            const uint8_t *v_in;
            uint8_t *y_out;
            uint8_t *u_out;
            uint8_t *v_out;
        };

        [[nodiscard]] yuv420_reconstruction_planes get_reconstruction_planes(const GstMapInfo &packed_map_info,
                                                                             const GstMapInfo &reconstructed_map_info,
                                                                             const GstVideoInfo &packed_info,
                                                                             const GstVideoInfo &reconstructed_info) {
            const auto *const y_in = packed_map_info.data;
            const auto *const u_in = y_in + packed_info.width * packed_info.height;
            const auto *const v_in = u_in + packed_info.width / 2 * (packed_info.height / 2);

            auto *const y_out = reconstructed_map_info.data;
            auto *const u_out = y_out + reconstructed_info.width * reconstructed_info.height;
            auto *const v_out = u_out + reconstructed_info.width / 2 * (reconstructed_info.height / 2);

            return {y_in, u_in, v_in, y_out, u_out, v_out};
        }

        /**
         * @brief Gets stride values for YUV420 reconstruction.
         * @param packed_info Packed frame video info.
         * @param reconstructed_info Reconstructed frame video info.
         * @return Tuple of strides (luma_stride_in, uv_stride_in, luma_stride_out, uv_stride_out).
         */
        [[nodiscard]] auto get_reconstruction_strides(const GstVideoInfo &packed_info,
                                                      const GstVideoInfo &reconstructed_info) {
            const int luma_stride_in = packed_info.width;
            const int uv_stride_in = packed_info.width / 2;
            const int luma_stride_out = reconstructed_info.width;
            const int uv_stride_out = reconstructed_info.width / 2;
            return std::make_tuple(luma_stride_in, uv_stride_in, luma_stride_out, uv_stride_out);
        }

        /**
         * @brief Gets plane heights for YUV420 reconstruction.
         * @param packed_info Packed frame video info.
         * @param reconstructed_info Reconstructed frame video info.
         * @return Tuple of heights (luma_plane_height_in, uv_plane_height_in, luma_plane_height_out,
         * uv_plane_height_out).
         */
        [[nodiscard]] auto get_reconstruction_heights(const GstVideoInfo &packed_info,
                                                      const GstVideoInfo &reconstructed_info) {
            const int luma_plane_height_in = packed_info.height;
            const int uv_plane_height_in = packed_info.height / 2;
            const int luma_plane_height_out = reconstructed_info.height;
            const int uv_plane_height_out = reconstructed_info.height / 2;
            return std::make_tuple(luma_plane_height_in, uv_plane_height_in, luma_plane_height_out,
                                   uv_plane_height_out);
        }

        /**
         * @brief Copies Y plane for a single rectangle during reconstruction.
         * @param planes Plane pointers.
         * @param strides Stride values.
         * @param heights Plane heights.
         * @param packed_rect Packed rectangle.
         * @param target_rect Target rectangle.
         */
        void copy_y_plane_reconstruction(const yuv420_reconstruction_planes &planes,
                                         const std::tuple<int, int, int, int> &strides,
                                         const std::tuple<int, int, int, int> &heights, const rectangle &packed_rect,
                                         const rectangle &target_rect) {
            const int luma_stride_in = std::get<0>(strides);
            const int luma_stride_out = std::get<2>(strides);
            const int luma_plane_height_in = std::get<0>(heights);
            const int luma_plane_height_out = std::get<2>(heights);

            const auto region = area_copy_internal::clamp_copy_region(
                    packed_rect.left, packed_rect.top, target_rect.left, target_rect.top, packed_rect.width,
                    packed_rect.height, luma_stride_in, luma_plane_height_in, luma_stride_out, luma_plane_height_out);

            if (!region.is_valid()) {
                warning(area_copy,
                        "copy_y_plane_reconstruction: Clamped copy invalid for source (%d,%d,%d,%d) and destination "
                        "(%d,%d,%d,%d). Skipping copy.",
                        packed_rect.left, packed_rect.top, packed_rect.width, packed_rect.height, target_rect.left,
                        target_rect.top, target_rect.width, target_rect.height);
                return;
            }

            copy_plane_region(planes.y_in, planes.y_out, luma_stride_in, luma_stride_out, luma_plane_height_in,
                              luma_plane_height_out, region.src_x, region.src_y, region.dst_x, region.dst_y,
                              region.width, region.height);
        }

        /**
         * @brief Copies UV planes for a single rectangle during reconstruction.
         * @param planes Plane pointers.
         * @param strides Stride values.
         * @param heights Plane heights.
         * @param packed_rect Packed rectangle.
         * @param target_rect Target rectangle.
         */
        void copy_uv_planes_reconstruction(const yuv420_reconstruction_planes &planes,
                                           const std::tuple<int, int, int, int> &strides,
                                           const std::tuple<int, int, int, int> &heights, const rectangle &packed_rect,
                                           const rectangle &target_rect) {
            const int uv_stride_in = std::get<1>(strides);
            const int uv_stride_out = std::get<3>(strides);
            const int uv_plane_height_in = std::get<1>(heights);
            const int uv_plane_height_out = std::get<3>(heights);

            const int src_uv_x = packed_rect.left / 2;
            const int src_uv_y = packed_rect.top / 2;
            const int dst_uv_x = target_rect.left / 2;
            const int dst_uv_y = target_rect.top / 2;
            const int uv_w = packed_rect.width / 2;
            const int uv_h = packed_rect.height / 2;

            const auto region = area_copy_internal::clamp_copy_region(src_uv_x, src_uv_y, dst_uv_x, dst_uv_y, uv_w,
                                                                      uv_h, uv_stride_in, uv_plane_height_in,
                                                                      uv_stride_out, uv_plane_height_out);
            if (!region.is_valid()) {
                warning(area_copy,
                        "copy_uv_planes_reconstruction: Clamped UV copy invalid for source (%d,%d,%d,%d) and "
                        "destination (%d,%d,%d,%d). Skipping copy.",
                        src_uv_x, src_uv_y, uv_w, uv_h, dst_uv_x, dst_uv_y, uv_w, uv_h);
                return;
            }

            copy_plane_region(planes.u_in, planes.u_out, uv_stride_in, uv_stride_out, uv_plane_height_in,
                              uv_plane_height_out, region.src_x, region.src_y, region.dst_x, region.dst_y, region.width,
                              region.height);
            copy_plane_region(planes.v_in, planes.v_out, uv_stride_in, uv_stride_out, uv_plane_height_in,
                              uv_plane_height_out, region.src_x, region.src_y, region.dst_x, region.dst_y, region.width,
                              region.height);
        }
    } // namespace

    /**
     * @brief Reconstructs a YUV420 frame by copying packed regions to their target locations.
     * @param packed_map_info Mapped input buffer (packed frame).
     * @param reconstructed_map_info Mapped output buffer (reconstructed frame).
     * @param packed_info Video info for the packed frame.
     * @param reconstructed_info Video info for the reconstructed frame.
     * @param packed_rects Vector of rectangles in the packed frame.
     * @param target_rects Vector of rectangles in the reconstructed (target) frame.
     */
    void reconstruct_yuv420_frame(const GstMapInfo &packed_map_info, const GstMapInfo &reconstructed_map_info,
                                  const GstVideoInfo &packed_info, const GstVideoInfo &reconstructed_info,
                                  const std::vector<rectangle> &packed_rects,
                                  const std::vector<rectangle> &target_rects) {
        const auto planes =
                get_reconstruction_planes(packed_map_info, reconstructed_map_info, packed_info, reconstructed_info);
        const auto strides = get_reconstruction_strides(packed_info, reconstructed_info);
        const auto heights = get_reconstruction_heights(packed_info, reconstructed_info);

        for (size_t rect_index = 0; rect_index < packed_rects.size(); ++rect_index) {
            const auto &packed_rect = packed_rects[rect_index];
            const auto &target_rect = target_rects[rect_index];

            copy_y_plane_reconstruction(planes, strides, heights, packed_rect, target_rect);
            copy_uv_planes_reconstruction(planes, strides, heights, packed_rect, target_rect);
        }
    }
} // namespace sf
