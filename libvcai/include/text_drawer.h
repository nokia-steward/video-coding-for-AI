// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <optional>
#include <string>
#include "yuv_color.h"

/**
 * @file text_drawer.h
 * @brief Provides functionality for drawing text on YUV video frames.
 *
 * This module provides general-purpose text drawing functions that can be used
 * to draw text overlays on NV12 and I420 video frames. It uses bitmap character
 * patterns for rendering digits, colons, and dots.
 *
 * This module follows functional programming principles:
 * - Pure character pattern definitions
 * - Immutable parameters: all input parameters are const references
 * - Clear separation of concerns: each function has a single responsibility
 * - Error handling through return values: functions return success/failure status
 *
 * Note: GStreamer buffer parameters cannot be const due to API requirements,
 * but the functions are designed to be as pure as possible within these constraints.
 */

namespace sf {
    /**
     * @brief Character dimensions for bitmap font.
     */
    constexpr int TEXT_CHAR_WIDTH = 8;
    constexpr int TEXT_CHAR_HEIGHT = 12;

    /**
     * @brief Character dimensions for large bitmap font (2x size).
     */
    constexpr int TEXT_CHAR_WIDTH_LARGE = 16;
    constexpr int TEXT_CHAR_HEIGHT_LARGE = 24;

    /**
     * @enum drawable_character
     * @brief Defines drawable character types supported by the text drawer.
     */
    enum class drawable_character {
        digit_0,
        digit_1,
        digit_2,
        digit_3,
        digit_4,
        digit_5,
        digit_6,
        digit_7,
        digit_8,
        digit_9,
        colon,
        dot,
        k_lowercase,
        b_lowercase,
        p_lowercase,
        s_lowercase,
        m_lowercase,
        f_lowercase,
        d_lowercase
    };

    /**
     * @brief Converts a character to drawable_character enum.
     * @param c The character to convert.
     * @return The drawable_character enum value, or std::nullopt if character is not supported.
     */
    [[nodiscard]] std::optional<drawable_character> char_to_drawable_character(char c);

    /**
     * @brief Draws a text string on an NV12 frame.
     *
     * This function draws a text string on an NV12 frame at the specified position.
     * Supported characters: digits (0-9), colon (:), and dot (.).
     * Other characters are skipped.
     *
     * @param buffer GstBuffer containing NV12 frame.
     * @param video_info GstVideoInfo for the frame.
     * @param text Text string to draw.
     * @param x X position in frame (top-left corner of text).
     * @param y Y position in frame (top-left corner of text).
     * @param color Color to use for the text (default: white).
     * @return true if successful, false otherwise.
     *
     * Note: GstBuffer parameter cannot be const due to GStreamer API requirements.
     * The function modifies the buffer content to draw text.
     */
    [[nodiscard]] bool draw_text_on_nv12_frame(GstBuffer *buffer, const GstVideoInfo *video_info,
                                               const std::string &text, int x, int y,
                                               yuv_color color = yuv_color::white);

    /**
     * @brief Draws a text string on an I420 frame.
     *
     * This function draws a text string on an I420 frame at the specified position.
     * Supported characters: digits (0-9), colon (:), and dot (.).
     * Other characters are skipped.
     *
     * @param buffer GstBuffer containing I420 frame.
     * @param video_info GstVideoInfo for the frame.
     * @param text Text string to draw.
     * @param x X position in frame (top-left corner of text).
     * @param y Y position in frame (top-left corner of text).
     * @param color Color to use for the text (default: white).
     * @return true if successful, false otherwise.
     *
     * Note: GstBuffer parameter cannot be const due to GStreamer API requirements.
     * The function modifies the buffer content to draw text.
     */
    [[nodiscard]] bool draw_text_on_i420_frame(GstBuffer *buffer, const GstVideoInfo *video_info,
                                               const std::string &text, int x, int y,
                                               yuv_color color = yuv_color::white);

    /**
     * @brief Draws a text string on an already-mapped NV12 frame.
     *
     * This function draws a text string on an NV12 frame that is already mapped
     * using GstVideoFrame. This avoids multiple map/unmap cycles and reduces race conditions.
     *
     * @param frame Pointer to an already-mapped GstVideoFrame (must be mapped with GST_MAP_WRITE).
     * @param text Text string to draw.
     * @param x X position in frame (top-left corner of text).
     * @param y Y position in frame (top-left corner of text).
     * @return true if successful, false otherwise.
     *
     * Note: The frame must remain mapped until after this function returns. The caller is responsible
     * for unmapping the frame.
     */
    [[nodiscard]] bool draw_text_on_nv12_frame_mapped(GstVideoFrame *frame, const std::string &text, int x, int y);

    /**
     * @brief Draws a text string on an already-mapped I420 frame.
     *
     * This function draws a text string on an I420 frame that is already mapped
     * using GstVideoFrame. This avoids multiple map/unmap cycles and reduces race conditions.
     *
     * @param frame Pointer to an already-mapped GstVideoFrame (must be mapped with GST_MAP_WRITE).
     * @param text Text string to draw.
     * @param x X position in frame (top-left corner of text).
     * @param y Y position in frame (top-left corner of text).
     * @return true if successful, false otherwise.
     *
     * Note: The frame must remain mapped until after this function returns. The caller is responsible
     * for unmapping the frame.
     */
    [[nodiscard]] bool draw_text_on_i420_frame_mapped(GstVideoFrame *frame, const std::string &text, int x, int y);

    /**
     * @brief Draws a text string on an NV12 frame with large size (2x) and specified color.
     *
     * This function draws a text string on an NV12 frame at the specified position.
     * The text is rendered at 2x the normal size (16x24 pixels per character).
     * Supported characters: digits (0-9), colon (:), dot (.), and letters k, b, p, s, m, f, d.
     * Other characters are skipped.
     *
     * @param buffer GstBuffer containing NV12 frame.
     * @param video_info GstVideoInfo for the frame.
     * @param text Text string to draw.
     * @param x X position in frame (top-left corner of text).
     * @param y Y position in frame (top-left corner of text).
     * @param color Color to use for the text (default: red).
     * @return true if successful, false otherwise.
     *
     * Note: GstBuffer parameter cannot be const due to GStreamer API requirements.
     * The function modifies the buffer content to draw text.
     */
    [[nodiscard]] bool draw_text_on_nv12_frame_large_red(GstBuffer *buffer, const GstVideoInfo *video_info,
                                                         const std::string &text, int x, int y,
                                                         yuv_color color = yuv_color::red);

    /**
     * @brief Draws a text string on an I420 frame with large size (2x) and specified color.
     *
     * This function draws a text string on an I420 frame at the specified position.
     * The text is rendered at 2x the normal size (16x24 pixels per character).
     * Supported characters: digits (0-9), colon (:), dot (.), and letters k, b, p, s, m, f, d.
     * Other characters are skipped.
     *
     * @param buffer GstBuffer containing I420 frame.
     * @param video_info GstVideoInfo for the frame.
     * @param text Text string to draw.
     * @param x X position in frame (top-left corner of text).
     * @param y Y position in frame (top-left corner of text).
     * @param color Color to use for the text (default: red).
     * @return true if successful, false otherwise.
     *
     * Note: GstBuffer parameter cannot be const due to GStreamer API requirements.
     * The function modifies the buffer content to draw text.
     */
    [[nodiscard]] bool draw_text_on_i420_frame_large_red(GstBuffer *buffer, const GstVideoInfo *video_info,
                                                         const std::string &text, int x, int y,
                                                         yuv_color color = yuv_color::red);
} // namespace sf
