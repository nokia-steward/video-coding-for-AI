// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <algorithm>

namespace sf::area_copy_internal {
    struct clamped_copy_region {
        int src_x;
        int src_y;
        int dst_x;
        int dst_y;
        int width;
        int height;

        [[nodiscard]] bool is_valid() const { return width > 0 && height > 0; }
    };

    [[nodiscard]] inline int clamp_coordinate_to_limit(const int coordinate, const int limit) {
        if (limit <= 0) {
            return 0;
        }
        return std::clamp(coordinate, 0, limit - 1);
    }

    [[nodiscard]] inline clamped_copy_region clamp_copy_region(const int src_x, const int src_y, const int dst_x,
                                                               const int dst_y, const int requested_width,
                                                               const int requested_height, const int src_width_limit,
                                                               const int src_height_limit, const int dst_width_limit,
                                                               const int dst_height_limit) {
        if (requested_width <= 0 || requested_height <= 0 || src_width_limit <= 0 || src_height_limit <= 0 ||
            dst_width_limit <= 0 || dst_height_limit <= 0) {
            return {0, 0, 0, 0, 0, 0};
        }

        const int clamped_src_x = clamp_coordinate_to_limit(src_x, src_width_limit);
        const int clamped_src_y = clamp_coordinate_to_limit(src_y, src_height_limit);
        const int clamped_dst_x = clamp_coordinate_to_limit(dst_x, dst_width_limit);
        const int clamped_dst_y = clamp_coordinate_to_limit(dst_y, dst_height_limit);

        const int max_src_w = src_width_limit - clamped_src_x;
        const int max_src_h = src_height_limit - clamped_src_y;
        const int max_dst_w = dst_width_limit - clamped_dst_x;
        const int max_dst_h = dst_height_limit - clamped_dst_y;

        const int clamped_width = std::max(0, std::min({requested_width, max_src_w, max_dst_w}));
        const int clamped_height = std::max(0, std::min({requested_height, max_src_h, max_dst_h}));

        return {clamped_src_x, clamped_src_y, clamped_dst_x, clamped_dst_y, clamped_width, clamped_height};
    }
} // namespace sf::area_copy_internal
