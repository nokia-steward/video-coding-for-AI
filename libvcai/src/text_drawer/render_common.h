// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../include/text_drawer.h"

namespace sf {
    /**
     * @brief Lightweight YUV triplet used by text renderer kernels.
     */
    struct yuv_values {
        uint8_t luma;
        uint8_t chroma_u;
        uint8_t chroma_v;
    };

    /**
     * @brief Maps a logical text color to YUV values used for drawing.
     * @param color Requested text/background color.
     * @return Corresponding YUV value triplet.
     */
    [[nodiscard]] inline yuv_values get_yuv_values_for_color(const yuv_color color) {
        switch (color) {
            case yuv_color::blue:
                return {29, 255, 107};
            case yuv_color::purple:
                return {52, 169, 181};
            case yuv_color::red:
                return {76, 84, 255};
            case yuv_color::yellow_orange:
                return {180, 30, 200};
            case yuv_color::white:
                return {180, 128, 128};
            case yuv_color::dark_gray:
                return {50, 128, 128};
            default:
                return {180, 128, 128};
        }
    }

    /**
     * @brief Draws a filled rectangle on NV12 data planes.
     */
    void draw_background_rect_nv12(uint8_t *y_plane, uint8_t *uv_plane, int y_stride, int uv_stride, int x, int y,
                                   int width, int height, int frame_width, int frame_height, yuv_color color);

    /**
     * @brief Draws a single 8x12 glyph on NV12 data planes.
     */
    void draw_char_on_nv12(uint8_t *y_plane, uint8_t *uv_plane, int y_stride, int uv_stride, int x, int y,
                           const bool pattern[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH], int width, int height,
                           yuv_color color);

    /**
     * @brief Draws a single 2x scaled glyph on NV12 data planes.
     */
    void draw_char_on_nv12_large_red(uint8_t *y_plane, uint8_t *uv_plane, int y_stride, int uv_stride, int x, int y,
                                     const bool pattern[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH], int width, int height,
                                     yuv_color color);

    /**
     * @brief Draws a filled rectangle on I420 data planes.
     */
    void draw_background_rect_i420(uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane, int y_stride, int u_stride,
                                   int chroma_v_stride, int x, int y, int width, int height, int frame_width,
                                   int frame_height, yuv_color color);

    /**
     * @brief Draws a single 8x12 glyph on I420 data planes.
     */
    void draw_char_on_i420(uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane, int y_stride, int u_stride,
                           int chroma_v_stride, int x, int y, const bool pattern[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH],
                           int width, int height, yuv_color color);

    /**
     * @brief Draws a single 2x scaled glyph on I420 data planes.
     */
    void draw_char_on_i420_large_red(uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane, int y_stride, int u_stride,
                                     int chroma_v_stride, int x, int y,
                                     const bool pattern[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH], int width, int height,
                                     yuv_color color);
} // namespace sf
