// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"
#include "plane_operations.h"

#include <cstring>
#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <vector>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Sanitizes rectangles for NV12 reconstruction (ensures even alignment).
         * @param rect Rectangle to sanitize.
         * @return Sanitized rectangle.
         */
        [[nodiscard]] rectangle sanitize_rect_for_reconstruction(const rectangle &rect) {
            rectangle sanitized = rect;
            sanitized.left &= ~1;
            sanitized.top &= ~1;
            sanitized.width &= ~1;
            sanitized.height &= ~1;
            return sanitized;
        }

        /**
         * @brief Copies Y plane rows for a single ROI during reconstruction.
         * @param src_y Source Y plane pointer.
         * @param dst_y Destination Y plane pointer.
         * @param src_y_stride Source Y stride.
         * @param dst_y_stride Destination Y stride.
         * @param p_rect Packed rectangle.
         * @param t_rect Target rectangle.
         */
        void copy_y_plane_reconstruction_rows(const uint8_t *const src_y, uint8_t *const dst_y, const int src_y_stride,
                                              const int dst_y_stride, const rectangle &p_rect,
                                              const rectangle &t_rect) {
            for (int row = 0; row < p_rect.height; ++row) {
                std::memcpy(dst_y + ((t_rect.top + row) * dst_y_stride) + t_rect.left,
                            src_y + ((p_rect.top + row) * src_y_stride) + p_rect.left,
                            static_cast<size_t>(p_rect.width));
            }
        }

        /**
         * @brief Copies UV plane rows for a single ROI during reconstruction.
         * @param src_uv Source UV plane pointer.
         * @param dst_uv Destination UV plane pointer.
         * @param src_uv_stride Source UV stride.
         * @param dst_uv_stride Destination UV stride.
         * @param p_rect Packed rectangle.
         * @param t_rect Target rectangle.
         */
        void copy_uv_plane_reconstruction_rows(const uint8_t *const src_uv, uint8_t *const dst_uv,
                                               const int src_uv_stride, const int dst_uv_stride,
                                               const rectangle &p_rect, const rectangle &t_rect) {
            for (int row = 0; row < p_rect.height / 2; ++row) {
                std::memcpy(dst_uv + ((t_rect.top / 2 + row) * dst_uv_stride) + t_rect.left,
                            src_uv + ((p_rect.top / 2 + row) * src_uv_stride) + p_rect.left,
                            static_cast<size_t>(p_rect.width));
            }
        }

        /**
         * @brief Processes a single ROI during NV12 reconstruction.
         * @param src_y Source Y plane pointer.
         * @param dst_y Destination Y plane pointer.
         * @param src_uv Source UV plane pointer.
         * @param dst_uv Destination UV plane pointer.
         * @param src_y_stride Source Y stride.
         * @param dst_y_stride Destination Y stride.
         * @param src_uv_stride Source UV stride.
         * @param dst_uv_stride Destination UV stride.
         * @param p_rect Packed rectangle.
         * @param t_rect Target rectangle.
         * @param roi_index ROI index for logging.
         */
        void process_single_nv12_reconstruction_roi(const uint8_t *const src_y, uint8_t *const dst_y,
                                                    const uint8_t *const src_uv, uint8_t *const dst_uv,
                                                    const int src_y_stride, const int dst_y_stride,
                                                    const int src_uv_stride, const int dst_uv_stride,
                                                    const rectangle &p_rect, const rectangle &t_rect,
                                                    const size_t roi_index) {
            const rectangle packed_rect_sanitized = sanitize_rect_for_reconstruction(p_rect);
            const rectangle target_rect_sanitized = sanitize_rect_for_reconstruction(t_rect);

            if (packed_rect_sanitized.width <= 0 || packed_rect_sanitized.height <= 0) {
                warning(area_copy, "ROI %zu has invalid dimensions after sanitization, skipping", roi_index);
                return;
            }

            debug(area_copy,
                  "Copying ROI %zu: packed rect (%d,%d) %dx%d -> target rect (%d,%d) %dx%d (after sanitization)",
                  roi_index, packed_rect_sanitized.left, packed_rect_sanitized.top, packed_rect_sanitized.width,
                  packed_rect_sanitized.height, target_rect_sanitized.left, target_rect_sanitized.top,
                  target_rect_sanitized.width, target_rect_sanitized.height);

            copy_y_plane_reconstruction_rows(src_y, dst_y, src_y_stride, dst_y_stride, packed_rect_sanitized,
                                             target_rect_sanitized);
            copy_uv_plane_reconstruction_rows(src_uv, dst_uv, src_uv_stride, dst_uv_stride, packed_rect_sanitized,
                                              target_rect_sanitized);
        }
    } // namespace

    /**
     * @brief Reconstructs an NV12 frame using GstVideoFrame for safe stride/memory handling.
     *
     * Follows the NV12 Video Region Reconstruction protocol:
     * - Uses GstVideoFrame API to handle complex padding and hardware buffers safely
     * - Enforces even-aligned coordinates using bitwise operations
     * - Uses byte-equivalent X coordinates for UV plane (x pixels = x bytes)
     * - Copies w bytes for UV plane (w/2 pixels * 2 bytes = w bytes)
     *
     * @param src_buf Source buffer (packed frame).
     * @param dst_buf Destination buffer (reconstructed frame).
     * @param src_info Video info for the packed frame.
     * @param dst_info Video info for the reconstructed frame.
     * @param packed_rects Vector of rectangles in the packed frame.
     * @param target_rects Vector of rectangles in the reconstructed (target) frame.
     */
    void reconstruct_nv12_frame(const GstBuffer *src_buf, unique_gst_buffer_ptr &dst_buf, GstVideoInfo *src_info,
                                GstVideoInfo *dst_info, const std::vector<rectangle> &packed_rects,
                                const std::vector<rectangle> &target_rects) {
        GstVideoFrame src_frame;
        GstVideoFrame dst_frame;

        if (!gst_video_frame_map(&src_frame, src_info, const_cast<GstBuffer *>(src_buf), GST_MAP_READ)) {
            error(area_copy, "Failed to map source frame");
            return;
        }

        if (!gst_video_frame_map(&dst_frame, dst_info, dst_buf.get(), GST_MAP_WRITE)) {
            gst_video_frame_unmap(&src_frame);
            error(area_copy, "Failed to map dest frame");
            return;
        }

        const uint8_t *src_y = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&src_frame, 0));
        uint8_t *dst_y = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&dst_frame, 0));
        const int src_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&src_frame, 0);
        const int dst_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&dst_frame, 0);

        const uint8_t *src_uv = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&src_frame, 1));
        uint8_t *dst_uv = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&dst_frame, 1));
        const int src_uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&src_frame, 1);
        const int dst_uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&dst_frame, 1);

        debug(area_copy,
              "NV12 reconstruction: packed=%dx%d (Y stride=%d, UV stride=%d), reconstructed=%dx%d (Y stride=%d, UV "
              "stride=%d)",
              src_info->width, src_info->height, src_y_stride, src_uv_stride, dst_info->width, dst_info->height,
              dst_y_stride, dst_uv_stride);

        for (size_t i = 0; i < packed_rects.size(); ++i) {
            process_single_nv12_reconstruction_roi(src_y, dst_y, src_uv, dst_uv, src_y_stride, dst_y_stride,
                                                   src_uv_stride, dst_uv_stride, packed_rects[i], target_rects[i], i);
        }

        gst_video_frame_unmap(&dst_frame);
        gst_video_frame_unmap(&src_frame);
    }
} // namespace sf
