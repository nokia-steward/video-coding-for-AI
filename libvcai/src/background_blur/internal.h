// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <vector>

#include <gst/video/video.h>

#include "../../include/rectangle.h"

namespace sf::background_blur_internal {
    struct roi_mask {
        int width;
        int height;
        std::vector<uint8_t> pixels;
        std::vector<uint8_t> row_all_roi;
        std::vector<uint8_t> col_all_roi;

        [[nodiscard]] bool contains(int x, int y) const;
        [[nodiscard]] bool is_row_all_roi(int y) const;
        [[nodiscard]] bool is_column_all_roi(int x) const;
    };

    [[nodiscard]] int calculate_kernel_size(double sigma);

    [[nodiscard]] std::vector<double> generate_gaussian_kernel(int kernel_size, double sigma);

    [[nodiscard]] rectangle scale_roi_for_subsampled_plane(const rectangle &roi, int scale_x, int scale_y,
                                                           int plane_width, int plane_height);

    [[nodiscard]] std::vector<rectangle> scale_rois_for_plane(const std::vector<rectangle> &rois, int scale_x,
                                                              int scale_y, int plane_width, int plane_height);

    [[nodiscard]] roi_mask build_roi_mask(int width, int height, const std::vector<rectangle> &rois);

    void apply_horizontal_blur_row(const uint8_t *src_row, uint8_t *dst_row, int row_width,
                                   const std::vector<double> &kernel, int kernel_size, int y_coord,
                                   const roi_mask &mask);

    void apply_vertical_blur_column(const uint8_t *src_plane, uint8_t *dst_plane, int stride, int height,
                                    const std::vector<double> &kernel, int kernel_size, int x_coord,
                                    const roi_mask &mask);

    [[nodiscard]] bool blur_plane(GstVideoFrame *frame, guint plane_index, const std::vector<double> &kernel,
                                  int kernel_size, int width, int height, const std::vector<rectangle> &rois,
                                  uint8_t *temp_buffer);

    [[nodiscard]] bool blur_nv12_frame(GstVideoFrame *frame, const std::vector<double> &kernel, int kernel_size,
                                       int width, int height, const std::vector<rectangle> &rois);

    [[nodiscard]] bool blur_yuv420_frame(GstVideoFrame *frame, const std::vector<double> &kernel, int kernel_size,
                                         int width, int height, const std::vector<rectangle> &rois);
} // namespace sf::background_blur_internal
