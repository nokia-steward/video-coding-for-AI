// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../include/text_drawer.h"

#include <gst/video/video.h>

#include "text_drawer/char_mapping.h"
#include "text_drawer/glyph_patterns.h"
#include "text_drawer/render_common.h"

namespace sf {
    namespace {
        enum class text_width_mode { all_characters, drawable_characters_only };

        struct text_render_style {
            int char_width;
            int char_height;
            int char_spacing;
            yuv_color background_color;
            text_width_mode width_mode;
        };

        using nv12_draw_character_function = void (*)(uint8_t *, uint8_t *, int, int, int, int,
                                                      const bool[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH], int, int,
                                                      yuv_color);

        using i420_draw_character_function = void (*)(uint8_t *, uint8_t *, uint8_t *, int, int, int, int, int,
                                                      const bool[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH], int, int,
                                                      yuv_color);

        /**
         * @brief Resolves a glyph bitmap for one character.
         * @param c Input character.
         * @return Glyph pattern pointer, or nullptr for unsupported characters.
         */
        [[nodiscard]] const bool (*get_char_pattern(const char c))[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH] {
            const auto char_opt = char_to_drawable_character(c);
            if (!char_opt) {
                return nullptr;
            }
            return get_char_pattern_from_enum(*char_opt);
        }

        /**
         * @brief Counts characters for background-width calculation.
         * @param text Text to render.
         * @param mode Counting mode for width calculation.
         * @return Count of characters contributing to width.
         */
        [[nodiscard]] int count_characters_for_width(const std::string &text, const text_width_mode mode) {
            if (mode == text_width_mode::all_characters) {
                return static_cast<int>(text.length());
            }

            int drawable_count = 0;
            for (const char c: text) {
                if (get_char_pattern(c)) {
                    ++drawable_count;
                }
            }
            return drawable_count;
        }

        /**
         * @brief Converts a character count to pixel width for the configured spacing.
         * @param char_count Number of characters to represent.
         * @param char_width Character width in pixels.
         * @param char_spacing Spacing between characters in pixels.
         * @return Total text width in pixels.
         */
        [[nodiscard]] int calculate_text_width(const int char_count, const int char_width, const int char_spacing) {
            if (char_count <= 0) {
                return 0;
            }
            return char_count * (char_width + char_spacing) - char_spacing;
        }

        /**
         * @brief Renders characters on NV12 planes using the provided kernel.
         */
        void draw_text_characters_nv12(uint8_t *y_plane, uint8_t *uv_plane, const int luma_stride, const int uv_stride,
                                       const std::string &text, const int start_x, const int y, const int width,
                                       const int height, const int char_width, const int char_spacing,
                                       const yuv_color color, const nv12_draw_character_function draw_character_fn) {
            int char_x = start_x;
            for (const char c: text) {
                if (char_x + char_width > width) {
                    break;
                }

                const auto *pattern = get_char_pattern(c);
                if (pattern) {
                    draw_character_fn(y_plane, uv_plane, luma_stride, uv_stride, char_x, y, *pattern, width, height,
                                      color);
                }
                char_x += char_width + char_spacing;
            }
        }

        /**
         * @brief Renders characters on I420 planes using the provided kernel.
         */
        void draw_text_characters_i420(uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane, const int luma_stride,
                                       const int chroma_u_stride, const int chroma_v_stride, const std::string &text,
                                       const int start_x, const int y, const int width, const int height,
                                       const int char_width, const int char_spacing, const yuv_color color,
                                       const i420_draw_character_function draw_character_fn) {
            int char_x = start_x;
            for (const char c: text) {
                if (char_x + char_width > width) {
                    break;
                }

                const auto *pattern = get_char_pattern(c);
                if (pattern) {
                    draw_character_fn(y_plane, u_plane, v_plane, luma_stride, chroma_u_stride, chroma_v_stride, char_x,
                                      y, *pattern, width, height, color);
                }
                char_x += char_width + char_spacing;
            }
        }

        /**
         * @brief Draws text on a mapped NV12 frame using a configurable style.
         */
        [[nodiscard]] bool draw_text_on_nv12_mapped(GstVideoFrame *frame, const std::string &text, const int origin_x,
                                                    const int origin_y, const yuv_color color,
                                                    const text_render_style &style,
                                                    const nv12_draw_character_function draw_character_fn) {
            if (!frame || text.empty()) {
                return false;
            }

            const int width = GST_VIDEO_FRAME_WIDTH(frame);
            const int height = GST_VIDEO_FRAME_HEIGHT(frame);
            if (width <= 0 || height <= 0) {
                return false;
            }

            auto *y_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));
            auto *uv_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 1));
            const int luma_stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);
            const int uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 1);

            const int character_count = count_characters_for_width(text, style.width_mode);
            const int text_width = calculate_text_width(character_count, style.char_width, style.char_spacing);
            if (text_width > 0) {
                constexpr int padding = 2;
                const int bg_x = origin_x - padding;
                const int bg_y = origin_y - padding;
                const int bg_width = text_width + padding * 2;
                const int bg_height = style.char_height + padding * 2;
                draw_background_rect_nv12(y_plane, uv_plane, luma_stride, uv_stride, bg_x, bg_y, bg_width, bg_height,
                                          width, height, style.background_color);
            }

            draw_text_characters_nv12(y_plane, uv_plane, luma_stride, uv_stride, text, origin_x, origin_y, width,
                                      height, style.char_width, style.char_spacing, color, draw_character_fn);
            return true;
        }

        /**
         * @brief Draws text on a mapped I420 frame using a configurable style.
         */
        [[nodiscard]] bool draw_text_on_i420_mapped(GstVideoFrame *frame, const std::string &text, const int origin_x,
                                                    const int origin_y, const yuv_color color,
                                                    const text_render_style &style,
                                                    const i420_draw_character_function draw_character_fn) {
            if (!frame || text.empty()) {
                return false;
            }

            const int width = GST_VIDEO_FRAME_WIDTH(frame);
            const int height = GST_VIDEO_FRAME_HEIGHT(frame);
            if (width <= 0 || height <= 0) {
                return false;
            }

            auto *y_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));
            auto *u_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 1));
            auto *v_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 2));
            const int luma_stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);
            const int chroma_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 1);
            const int chroma_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 2);

            const int character_count = count_characters_for_width(text, style.width_mode);
            const int text_width = calculate_text_width(character_count, style.char_width, style.char_spacing);
            if (text_width > 0) {
                constexpr int padding = 2;
                const int bg_x = origin_x - padding;
                const int bg_y = origin_y - padding;
                const int bg_width = text_width + padding * 2;
                const int bg_height = style.char_height + padding * 2;
                draw_background_rect_i420(y_plane, u_plane, v_plane, luma_stride, chroma_u_stride, chroma_v_stride,
                                          bg_x, bg_y, bg_width, bg_height, width, height, style.background_color);
            }

            draw_text_characters_i420(y_plane, u_plane, v_plane, luma_stride, chroma_u_stride, chroma_v_stride, text,
                                      origin_x, origin_y, width, height, style.char_width, style.char_spacing, color,
                                      draw_character_fn);
            return true;
        }

        /**
         * @brief Maps a writable video frame, invokes renderer, and unmaps it.
         */
        template<typename RenderFn>
        [[nodiscard]] bool draw_text_on_buffer_frame(GstBuffer *buffer, const GstVideoInfo *video_info,
                                                     RenderFn render_function) {
            if (!buffer || !video_info) {
                return false;
            }

            const int width = GST_VIDEO_INFO_WIDTH(video_info);
            const int height = GST_VIDEO_INFO_HEIGHT(video_info);
            if (width <= 0 || height <= 0) {
                return false;
            }

            GstVideoFrame frame{};
            if (!gst_video_frame_map(&frame, video_info, buffer, GST_MAP_WRITE)) {
                return false;
            }

            const bool success = render_function(&frame);
            gst_video_frame_unmap(&frame);
            return success;
        }

        constexpr text_render_style normal_text_style{TEXT_CHAR_WIDTH, TEXT_CHAR_HEIGHT, 1, yuv_color::dark_gray,
                                                      text_width_mode::all_characters};

        // Preserve existing NV12-mapped width semantics (background width counts only drawable glyphs).
        constexpr text_render_style normal_text_nv12_mapped_style{
                TEXT_CHAR_WIDTH, TEXT_CHAR_HEIGHT, 1, yuv_color::dark_gray, text_width_mode::drawable_characters_only};

        constexpr text_render_style large_text_style{TEXT_CHAR_WIDTH_LARGE, TEXT_CHAR_HEIGHT_LARGE, 2, yuv_color::white,
                                                     text_width_mode::all_characters};
    } // namespace

    bool draw_text_on_nv12_frame(GstBuffer *buffer, const GstVideoInfo *video_info, const std::string &text,
                                 const int origin_x, const int origin_y, const yuv_color color) {
        return draw_text_on_buffer_frame(buffer, video_info, [&](GstVideoFrame *frame) {
            return draw_text_on_nv12_mapped(frame, text, origin_x, origin_y, color, normal_text_style,
                                            draw_char_on_nv12);
        });
    }

    bool draw_text_on_i420_frame(GstBuffer *buffer, const GstVideoInfo *video_info, const std::string &text,
                                 const int origin_x, const int origin_y, const yuv_color color) {
        return draw_text_on_buffer_frame(buffer, video_info, [&](GstVideoFrame *frame) {
            return draw_text_on_i420_mapped(frame, text, origin_x, origin_y, color, normal_text_style,
                                            draw_char_on_i420);
        });
    }

    bool draw_text_on_nv12_frame_mapped(GstVideoFrame *frame, const std::string &text, const int origin_x,
                                        const int origin_y, const yuv_color color) {
        return draw_text_on_nv12_mapped(frame, text, origin_x, origin_y, color, normal_text_nv12_mapped_style,
                                        draw_char_on_nv12);
    }

    bool draw_text_on_i420_frame_mapped(GstVideoFrame *frame, const std::string &text, const int origin_x,
                                        const int origin_y, const yuv_color color) {
        return draw_text_on_i420_mapped(frame, text, origin_x, origin_y, color, normal_text_style, draw_char_on_i420);
    }

    bool draw_text_on_nv12_frame_large_red(GstBuffer *buffer, const GstVideoInfo *video_info, const std::string &text,
                                           const int origin_x, const int origin_y, const yuv_color color) {
        return draw_text_on_buffer_frame(buffer, video_info, [&](GstVideoFrame *frame) {
            return draw_text_on_nv12_mapped(frame, text, origin_x, origin_y, color, large_text_style,
                                            draw_char_on_nv12_large_red);
        });
    }

    bool draw_text_on_i420_frame_large_red(GstBuffer *buffer, const GstVideoInfo *video_info, const std::string &text,
                                           const int origin_x, const int origin_y, const yuv_color color) {
        return draw_text_on_buffer_frame(buffer, video_info, [&](GstVideoFrame *frame) {
            return draw_text_on_i420_mapped(frame, text, origin_x, origin_y, color, large_text_style,
                                            draw_char_on_i420_large_red);
        });
    }
} // namespace sf
