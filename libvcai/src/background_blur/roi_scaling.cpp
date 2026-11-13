// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "internal.h"

#include <algorithm>

namespace sf::background_blur_internal {
    bool roi_mask::contains(const int x, const int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return false;
        }
        const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
        return pixels[index] != 0;
    }

    bool roi_mask::is_row_all_roi(const int y) const {
        if (y < 0 || y >= static_cast<int>(row_all_roi.size())) {
            return false;
        }
        return row_all_roi[static_cast<size_t>(y)] != 0;
    }

    bool roi_mask::is_column_all_roi(const int x) const {
        if (x < 0 || x >= static_cast<int>(col_all_roi.size())) {
            return false;
        }
        return col_all_roi[static_cast<size_t>(x)] != 0;
    }

    rectangle scale_roi_for_subsampled_plane(const rectangle &roi, const int scale_x, const int scale_y,
                                             const int plane_width, const int plane_height) {
        const int start_x = std::clamp(roi.left / scale_x, 0, plane_width);
        const int start_y = std::clamp(roi.top / scale_y, 0, plane_height);
        const int end_x = std::clamp((roi.left + roi.width + (scale_x - 1)) / scale_x, 0, plane_width);
        const int end_y = std::clamp((roi.top + roi.height + (scale_y - 1)) / scale_y, 0, plane_height);

        const int scaled_w = std::max(0, end_x - start_x);
        const int scaled_h = std::max(0, end_y - start_y);
        return rectangle{start_x, start_y, scaled_w, scaled_h};
    }

    std::vector<rectangle> scale_rois_for_plane(const std::vector<rectangle> &rois, const int scale_x,
                                                const int scale_y, const int plane_width, const int plane_height) {
        std::vector<rectangle> scaled_rois;
        scaled_rois.reserve(rois.size());
        for (const auto &roi: rois) {
            const auto scaled = scale_roi_for_subsampled_plane(roi, scale_x, scale_y, plane_width, plane_height);
            if (scaled.is_valid()) {
                scaled_rois.push_back(scaled);
            }
        }
        return scaled_rois;
    }

    roi_mask build_roi_mask(const int width, const int height, const std::vector<rectangle> &rois) {
        const int safe_width = std::max(0, width);
        const int safe_height = std::max(0, height);
        const size_t pixel_count = static_cast<size_t>(safe_width) * static_cast<size_t>(safe_height);
        roi_mask mask{
                .width = width,
                .height = height,
                .pixels = std::vector<uint8_t>(pixel_count, 0),
                .row_all_roi = std::vector<uint8_t>(static_cast<size_t>(safe_height), 0),
                .col_all_roi = std::vector<uint8_t>(static_cast<size_t>(safe_width), 0),
        };

        if (width <= 0 || height <= 0 || rois.empty()) {
            return mask;
        }

        for (const auto &roi: rois) {
            const int start_x = std::clamp(roi.left, 0, width);
            const int start_y = std::clamp(roi.top, 0, height);
            const int end_x = std::clamp(roi.left + roi.width, 0, width);
            const int end_y = std::clamp(roi.top + roi.height, 0, height);
            if (start_x >= end_x || start_y >= end_y) {
                continue;
            }

            for (int y = start_y; y < end_y; ++y) {
                for (int x = start_x; x < end_x; ++x) {
                    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    mask.pixels[index] = 1;
                }
            }
        }

        for (int y = 0; y < height; ++y) {
            bool all_roi = true;
            for (int x = 0; x < width; ++x) {
                if (!mask.contains(x, y)) {
                    all_roi = false;
                    break;
                }
            }
            mask.row_all_roi[static_cast<size_t>(y)] = all_roi ? 1 : 0;
        }

        for (int x = 0; x < width; ++x) {
            bool all_roi = true;
            for (int y = 0; y < height; ++y) {
                if (!mask.contains(x, y)) {
                    all_roi = false;
                    break;
                }
            }
            mask.col_all_roi[static_cast<size_t>(x)] = all_roi ? 1 : 0;
        }

        return mask;
    }
} // namespace sf::background_blur_internal
