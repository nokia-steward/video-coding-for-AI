// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "internal.h"

#include <memory>

namespace sf::background_blur_internal {
    namespace {
        void extract_uv_planes(const uint8_t *uv_plane, const int uv_stride, const int uv_width, const int uv_height,
                               uint8_t *u_buffer, uint8_t *v_buffer) {
            for (int y = 0; y < uv_height; ++y) {
                for (int x = 0; x < uv_width; ++x) {
                    const size_t index =
                            static_cast<size_t>(y) * static_cast<size_t>(uv_width) + static_cast<size_t>(x);
                    u_buffer[index] = uv_plane[y * uv_stride + x * 2];
                    v_buffer[index] = uv_plane[y * uv_stride + x * 2 + 1];
                }
            }
        }

        void write_back_uv_planes(uint8_t *uv_plane, const int uv_stride, const int uv_width, const int uv_height,
                                  const uint8_t *u_buffer, const uint8_t *v_buffer) {
            for (int y = 0; y < uv_height; ++y) {
                for (int x = 0; x < uv_width; ++x) {
                    const size_t index =
                            static_cast<size_t>(y) * static_cast<size_t>(uv_width) + static_cast<size_t>(x);
                    uv_plane[y * uv_stride + x * 2] = u_buffer[index];
                    uv_plane[y * uv_stride + x * 2 + 1] = v_buffer[index];
                }
            }
        }

        void blur_single_chroma_plane(uint8_t *src_buffer, uint8_t *work_buffer, const int plane_width,
                                      const int plane_height, const std::vector<double> &kernel, const int kernel_size,
                                      const roi_mask &mask) {
            for (int y = 0; y < plane_height; ++y) {
                const uint8_t *src_row = src_buffer + static_cast<gsize>(y) * static_cast<gsize>(plane_width);
                uint8_t *dst_row = work_buffer + static_cast<gsize>(y) * static_cast<gsize>(plane_width);
                apply_horizontal_blur_row(src_row, dst_row, plane_width, kernel, kernel_size, y, mask);
            }

            for (int x = 0; x < plane_width; ++x) {
                apply_vertical_blur_column(work_buffer, src_buffer, plane_width, plane_height, kernel, kernel_size, x,
                                           mask);
            }
        }
    } // namespace

    bool blur_nv12_frame(GstVideoFrame *frame, const std::vector<double> &kernel, const int kernel_size,
                         const int width, const int height, const std::vector<rectangle> &rois) {
        const gint luma_plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, 0);
        const gint luma_plane_width = GST_VIDEO_INFO_COMP_WIDTH(&frame->info, 0);
        const gsize luma_temp_size = static_cast<gsize>(luma_plane_width) * static_cast<gsize>(luma_plane_height);
        const auto luma_temp_buffer = std::make_unique<uint8_t[]>(luma_temp_size);
        if (!blur_plane(frame, 0, kernel, kernel_size, width, height, rois, luma_temp_buffer.get())) {
            return false;
        }

        const gint uv_plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, 1);
        const gint uv_plane_width = GST_VIDEO_INFO_COMP_WIDTH(&frame->info, 1);
        const gsize uv_plane_size = static_cast<gsize>(uv_plane_width) * static_cast<gsize>(uv_plane_height);
        const gint uv_stride = GST_VIDEO_INFO_PLANE_STRIDE(&frame->info, 1);
        auto *uv_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 1));
        if (!uv_plane || uv_plane_width <= 0 || uv_plane_height <= 0 || uv_stride <= 0) {
            return false;
        }

        const auto uv_scaled_rois = scale_rois_for_plane(rois, 2, 2, uv_plane_width, uv_plane_height);
        const roi_mask uv_mask = build_roi_mask(uv_plane_width, uv_plane_height, uv_scaled_rois);

        auto chroma_u_src_buffer = std::make_unique<uint8_t[]>(uv_plane_size);
        auto chroma_v_src_buffer = std::make_unique<uint8_t[]>(uv_plane_size);
        const auto chroma_u_work_buffer = std::make_unique<uint8_t[]>(uv_plane_size);
        const auto chroma_v_work_buffer = std::make_unique<uint8_t[]>(uv_plane_size);

        extract_uv_planes(uv_plane, uv_stride, uv_plane_width, uv_plane_height, chroma_u_src_buffer.get(),
                          chroma_v_src_buffer.get());
        blur_single_chroma_plane(chroma_u_src_buffer.get(), chroma_u_work_buffer.get(), uv_plane_width, uv_plane_height,
                                 kernel, kernel_size, uv_mask);
        blur_single_chroma_plane(chroma_v_src_buffer.get(), chroma_v_work_buffer.get(), uv_plane_width, uv_plane_height,
                                 kernel, kernel_size, uv_mask);
        write_back_uv_planes(uv_plane, uv_stride, uv_plane_width, uv_plane_height, chroma_u_src_buffer.get(),
                             chroma_v_src_buffer.get());

        return true;
    }
} // namespace sf::background_blur_internal
