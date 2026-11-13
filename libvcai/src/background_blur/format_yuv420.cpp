// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace sf::background_blur_internal {
    void apply_horizontal_blur_row(const uint8_t *src_row, uint8_t *dst_row, const int row_width,
                                   const std::vector<double> &kernel, const int kernel_size, const int y_coord,
                                   const roi_mask &mask) {
        if (mask.is_row_all_roi(y_coord)) {
            std::memcpy(dst_row, src_row, static_cast<gsize>(row_width));
            return;
        }

        const int half_kernel = kernel_size / 2;
        for (int x = 0; x < row_width; ++x) {
            if (mask.contains(x, y_coord)) {
                dst_row[x] = src_row[x];
                continue;
            }

            double sum = 0.0;
            for (int k = 0; k < kernel_size; ++k) {
                const int src_x = std::clamp(x + k - half_kernel, 0, row_width - 1);
                sum += static_cast<double>(src_row[src_x]) * kernel[static_cast<size_t>(k)];
            }
            dst_row[x] = static_cast<uint8_t>(std::round(sum));
        }
    }

    void apply_vertical_blur_column(const uint8_t *src_plane, uint8_t *dst_plane, const int stride, const int height,
                                    const std::vector<double> &kernel, const int kernel_size, const int x_coord,
                                    const roi_mask &mask) {
        if (mask.is_column_all_roi(x_coord)) {
            for (int y = 0; y < height; ++y) {
                const int pixel_offset = y * stride + x_coord;
                dst_plane[pixel_offset] = src_plane[pixel_offset];
            }
            return;
        }

        const int half_kernel = kernel_size / 2;
        for (int y = 0; y < height; ++y) {
            const int pixel_offset = y * stride + x_coord;
            if (mask.contains(x_coord, y)) {
                dst_plane[pixel_offset] = src_plane[pixel_offset];
                continue;
            }

            double sum = 0.0;
            for (int k = 0; k < kernel_size; ++k) {
                const int src_y = std::clamp(y + k - half_kernel, 0, height - 1);
                const int src_offset = src_y * stride + x_coord;
                sum += static_cast<double>(src_plane[src_offset]) * kernel[static_cast<size_t>(k)];
            }
            dst_plane[pixel_offset] = static_cast<uint8_t>(std::round(sum));
        }
    }

    bool blur_plane(GstVideoFrame *frame, const guint plane_index, const std::vector<double> &kernel,
                    const int kernel_size, const int width, const int height, const std::vector<rectangle> &rois,
                    uint8_t *temp_buffer) {
        const gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, plane_index);
        const gint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE(&frame->info, plane_index);
        const gint comp_width = GST_VIDEO_INFO_COMP_WIDTH(&frame->info, plane_index);
        auto *plane_data = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, plane_index));

        if (!plane_data || plane_height <= 0 || comp_width <= 0 || plane_stride <= 0) {
            return false;
        }

        const int chroma_scale_x = (comp_width < width) ? 2 : 1;
        const int chroma_scale_y = (plane_height < height) ? 2 : 1;
        const auto scaled_rois =
                (plane_index > 0) ? scale_rois_for_plane(rois, chroma_scale_x, chroma_scale_y, comp_width, plane_height)
                                  : rois;
        const roi_mask mask = build_roi_mask(comp_width, plane_height, scaled_rois);

        for (int y = 0; y < plane_height; ++y) {
            const uint8_t *src_row = plane_data + static_cast<gsize>(y) * static_cast<gsize>(plane_stride);
            uint8_t *dst_row = temp_buffer + static_cast<gsize>(y) * static_cast<gsize>(comp_width);
            apply_horizontal_blur_row(src_row, dst_row, comp_width, kernel, kernel_size, y, mask);
        }

        for (int x = 0; x < comp_width; ++x) {
            apply_vertical_blur_column(temp_buffer, plane_data, comp_width, plane_height, kernel, kernel_size, x, mask);
        }

        return true;
    }

    bool blur_yuv420_frame(GstVideoFrame *frame, const std::vector<double> &kernel, const int kernel_size,
                           const int width, const int height, const std::vector<rectangle> &rois) {
        const gint luma_plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, 0);
        const gint luma_plane_width = GST_VIDEO_INFO_COMP_WIDTH(&frame->info, 0);
        const gsize luma_temp_size = static_cast<gsize>(luma_plane_width) * static_cast<gsize>(luma_plane_height);
        const auto luma_temp_buffer = std::make_unique<uint8_t[]>(luma_temp_size);
        if (!blur_plane(frame, 0, kernel, kernel_size, width, height, rois, luma_temp_buffer.get())) {
            return false;
        }

        const gint chroma_u_plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, 1);
        const gint chroma_u_plane_width = GST_VIDEO_INFO_COMP_WIDTH(&frame->info, 1);
        const gsize chroma_u_temp_size =
                static_cast<gsize>(chroma_u_plane_width) * static_cast<gsize>(chroma_u_plane_height);
        const auto chroma_u_temp_buffer = std::make_unique<uint8_t[]>(chroma_u_temp_size);
        if (!blur_plane(frame, 1, kernel, kernel_size, width, height, rois, chroma_u_temp_buffer.get())) {
            return false;
        }

        const gint chroma_v_plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&frame->info, 2);
        const gint chroma_v_plane_width = GST_VIDEO_INFO_COMP_WIDTH(&frame->info, 2);
        const gsize chroma_v_temp_size =
                static_cast<gsize>(chroma_v_plane_width) * static_cast<gsize>(chroma_v_plane_height);
        const auto chroma_v_temp_buffer = std::make_unique<uint8_t[]>(chroma_v_temp_size);
        if (!blur_plane(frame, 2, kernel, kernel_size, width, height, rois, chroma_v_temp_buffer.get())) {
            return false;
        }

        return true;
    }
} // namespace sf::background_blur_internal
