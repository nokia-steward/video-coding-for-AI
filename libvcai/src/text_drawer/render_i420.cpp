// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "render_common.h"

namespace sf {
    void draw_background_rect_i420(uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane, const int y_stride,
                                   const int chroma_u_stride, const int chroma_v_stride, const int x, const int y,
                                   const int width, const int height, const int frame_width, const int frame_height,
                                   const yuv_color color) {
        const auto [y_val, u_val, v_val] = get_yuv_values_for_color(color);

        for (int rect_y = 0; rect_y < height; ++rect_y) {
            const int frame_y = y + rect_y;
            if (frame_y < 0 || frame_y >= frame_height) {
                continue;
            }

            for (int rect_x = 0; rect_x < width; ++rect_x) {
                const int frame_x = x + rect_x;
                if (frame_x < 0 || frame_x >= frame_width) {
                    continue;
                }

                y_plane[frame_y * y_stride + frame_x] = y_val;

                const int uv_x = frame_x / 2;
                const int uv_y = frame_y / 2;
                if (uv_y < frame_height / 2 && uv_x < frame_width / 2) {
                    u_plane[uv_y * chroma_u_stride + uv_x] = u_val;
                    v_plane[uv_y * chroma_v_stride + uv_x] = v_val;
                }
            }
        }
    }

    void draw_char_on_i420(uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane, const int y_stride,
                           const int chroma_u_stride, const int chroma_v_stride, const int x, const int y,
                           const bool pattern[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH], const int width, const int height,
                           const yuv_color color) {
        const auto [y_val, u_val, v_val] = get_yuv_values_for_color(color);

        for (int char_y = 0; char_y < TEXT_CHAR_HEIGHT; ++char_y) {
            const int frame_y = y + char_y;
            if (frame_y < 0 || frame_y >= height) {
                continue;
            }

            for (int char_x = 0; char_x < TEXT_CHAR_WIDTH; ++char_x) {
                if (!pattern[char_y][char_x]) {
                    continue;
                }

                const int frame_x = x + char_x;
                if (frame_x < 0 || frame_x >= width) {
                    continue;
                }

                y_plane[frame_y * y_stride + frame_x] = y_val;

                const int uv_x = frame_x / 2;
                const int uv_y = frame_y / 2;
                if (uv_y < height / 2 && uv_x < width / 2) {
                    u_plane[uv_y * chroma_u_stride + uv_x] = u_val;
                    v_plane[uv_y * chroma_v_stride + uv_x] = v_val;
                }
            }
        }
    }

    void draw_char_on_i420_large_red(uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane, const int y_stride,
                                     const int chroma_u_stride, const int chroma_v_stride, const int x, const int y,
                                     const bool pattern[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH], const int width,
                                     const int height, const yuv_color color) {
        const auto [y_val, u_val, v_val] = get_yuv_values_for_color(color);

        for (int char_y = 0; char_y < TEXT_CHAR_HEIGHT; ++char_y) {
            for (int char_x = 0; char_x < TEXT_CHAR_WIDTH; ++char_x) {
                if (!pattern[char_y][char_x]) {
                    continue;
                }

                for (int scale_y = 0; scale_y < 2; ++scale_y) {
                    const int frame_y = y + char_y * 2 + scale_y;
                    if (frame_y < 0 || frame_y >= height) {
                        continue;
                    }

                    for (int scale_x = 0; scale_x < 2; ++scale_x) {
                        const int frame_x = x + char_x * 2 + scale_x;
                        if (frame_x < 0 || frame_x >= width) {
                            continue;
                        }

                        y_plane[frame_y * y_stride + frame_x] = y_val;

                        const int uv_x = frame_x / 2;
                        const int uv_y = frame_y / 2;
                        if (uv_y < height / 2 && uv_x < width / 2) {
                            u_plane[uv_y * chroma_u_stride + uv_x] = u_val;
                            v_plane[uv_y * chroma_v_stride + uv_x] = v_val;
                        }
                    }
                }
            }
        }
    }
} // namespace sf
