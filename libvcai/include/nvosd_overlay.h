// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include "rectangle.h"
#include "yuv_color.h"

/**
 * @file nvosd_overlay.h
 * @brief Helper utilities for adding NvDsDisplayMeta overlays.
 */

// Forward declarations for DeepStream types
struct _NvDsBatchMeta;
using NvDsBatchMeta = _NvDsBatchMeta;

namespace sf {
    /**
     * @brief Ensures a NvDsBatchMeta is attached to the buffer.
     *
     * Creates a batch meta with a single frame meta when missing.
     *
     * @param buffer The GstBuffer to attach metadata to.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @return The attached batch meta or nullptr on failure.
     */
    [[nodiscard]] NvDsBatchMeta *ensure_nvds_batch_meta(GstBuffer *buffer, int width, int height);

    /**
     * @brief Adds a rectangle overlay to the display metadata for a frame.
     *
     * @param batch_meta The batch metadata associated with the buffer.
     * @param rect Rectangle coordinates to draw.
     * @param color Border color.
     * @param border_width Border width in pixels.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool add_rect_to_display_meta(NvDsBatchMeta *batch_meta, const rectangle &rect, yuv_color color,
                                                int border_width);

    /**
     * @brief Adds a text overlay to the display metadata for a frame.
     *
     * @param batch_meta The batch metadata associated with the buffer.
     * @param text Text to render (UTF-8).
     * @param x X offset in pixels.
     * @param y Y offset in pixels.
     * @param color Text color.
     * @param font_size Font size in points (must be > 0).
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool add_text_to_display_meta(NvDsBatchMeta *batch_meta, const char *text, int x, int y,
                                                yuv_color color, int font_size);
} // namespace sf
