// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <vector>

#include "gst_raii.h"
#include "rectangle.h"

/**
 * @file background_blur.h
 * @brief Provides functionality for applying Gaussian blur to background areas of video frames.
 *
 * This module applies blur only to pixels that are NOT within any specified ROI (Region of Interest).
 * The ROIs remain sharp while the background is blurred, creating a privacy or focus effect.
 *
 * This module follows functional programming principles:
 * - Pure blur calculations: kernel coefficients are computed from input parameters
 * - Immutable parameters: all input parameters are const references
 * - Clear separation of concerns: each function has a single responsibility
 * - Error handling through return values: functions return success/failure status
 *
 * Note: GStreamer buffer parameters cannot be const due to API requirements,
 * but the functions are designed to be as pure as possible within these constraints.
 */

#include "area_copy.h"

namespace sf {
    /**
     * @brief Applies Gaussian blur to background areas of a video frame (excluding ROIs).
     *
     * This function blurs only the pixels that are NOT within any of the specified ROIs.
     * The ROIs remain unblurred, creating a focus effect where the background is blurred.
     *
     * The blur is implemented using a separable Gaussian kernel for performance:
     * - First pass: horizontal blur
     * - Second pass: vertical blur
     *
     * This approach is O(n) per pixel rather than O(n²) for a full 2D kernel.
     *
     * @param buffer The GstBuffer containing the frame data (will be modified).
     *                Uses RAII wrapper for safe buffer management.
     * @param rois Vector of rectangles defining regions to preserve (not blur).
     * @param sigma Standard deviation of the Gaussian kernel (controls blur strength).
     *              Typical range: 0.5 to 5.0. Higher values = more blur.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Video format enum value.
     * @param kernel_size Optional explicit kernel size (must be odd, e.g., 3, 5, 7, 9).
     *                    If 0, automatically calculated from sigma as: 2 * ceil(3 * sigma) + 1.
     * @return true if blur was applied successfully, false otherwise.
     *
     * Note: Buffer parameter is non-const reference because the function modifies the buffer content.
     * The buffer is managed by GStreamer, so we use unique_gst_buffer_no_delete_ptr.
     *
     * Supported formats: NV12, I420, YUV420
     */
    [[nodiscard]] bool apply_background_blur(const unique_gst_buffer_no_delete_ptr &buffer,
                                             const std::vector<rectangle> &rois, double sigma, int width, int height,
                                             video_format format, int kernel_size = 0);

} // namespace sf

/**
 * @brief Initialize the background_blur debug category.
 * This function should be called after gst_init() to ensure the category is properly initialized.
 */
void init_background_blur_debug_category();
