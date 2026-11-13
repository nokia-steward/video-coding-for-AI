// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/rectangle.h"
#include "frame_setup.h"
#include "uv_plane_copy.h"
#include "validation.h"
#include "y_plane_copy.h"

#include <gst/video/video-info.h>
#include <gst/video/video.h>
#include <vector>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Gets plane data pointers and strides from mapped frames.
         * @param input_frame Input video frame.
         * @param output_frame Output video frame.
         * @return Tuple of (y_in, y_out, luma_stride_in, luma_stride_out, uv_in, uv_out, uv_stride_in, uv_stride_out,
         * uv_plane_height).
         */
        [[nodiscard]] auto get_nv12_plane_data(const GstVideoFrame &input_frame, const GstVideoFrame &output_frame) {
            const uint8_t *y_in = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&input_frame, 0));
            uint8_t *y_out = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, 0));
            const int luma_stride_in = GST_VIDEO_FRAME_PLANE_STRIDE(&input_frame, 0);
            const int luma_stride_out = GST_VIDEO_FRAME_PLANE_STRIDE(&output_frame, 0);

            const uint8_t *uv_in = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&input_frame, 1));
            uint8_t *uv_out = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&output_frame, 1));
            const int uv_stride_in = GST_VIDEO_FRAME_PLANE_STRIDE(&input_frame, 1);
            const int uv_stride_out = GST_VIDEO_FRAME_PLANE_STRIDE(&output_frame, 1);
            const int uv_plane_height_in = GST_VIDEO_FRAME_COMP_HEIGHT(&input_frame, 1);
            const int uv_plane_height_out = GST_VIDEO_FRAME_COMP_HEIGHT(&output_frame, 1);

            return std::make_tuple(y_in, y_out, luma_stride_in, luma_stride_out, uv_in, uv_out, uv_stride_in,
                                   uv_stride_out, uv_plane_height_in, uv_plane_height_out);
        }

        /**
         * @brief Processes a single ROI for NV12 frame.
         * @param planes Plane data tuple.
         * @param input_rect Input rectangle.
         * @param packed_rect Packed rectangle.
         * @param max_output_x Maximum output X.
         * @param max_output_y Maximum output Y.
         * @param rect_index Index for logging.
         */
        void process_single_nv12_roi(const std::tuple<const uint8_t *, uint8_t *, int, int, const uint8_t *, uint8_t *,
                                                      int, int, int, int> &planes,
                                     const rectangle &input_rect, const rectangle &packed_rect, const int max_output_x,
                                     const int max_output_y, const size_t rect_index) {
            const auto [y_in, y_out, luma_stride_in, luma_stride_out, uv_in, uv_out, uv_stride_in, uv_stride_out,
                        uv_plane_height_in, uv_plane_height_out] = planes;

            const rectangle input_rect_even = sanitize_input_rectangle_nv12(input_rect);
            const rectangle packed_rect_even = sanitize_packed_rectangle_nv12(packed_rect);

            const auto copy_params_opt = validate_and_calculate_nv12_copy_params(
                    input_rect_even, packed_rect_even, max_output_x, max_output_y, rect_index);
            if (!copy_params_opt) {
                return;
            }

            const auto [clamped_packed_x, clamped_packed_y, copy_w_even, copy_h_even] = *copy_params_opt;

            debug(area_copy, "Copying ROI %zu: input (%d,%d) %dx%d -> packed (%d,%d) (copying %dx%d)", rect_index,
                  input_rect_even.left, input_rect_even.top, input_rect_even.width, input_rect_even.height,
                  clamped_packed_x, clamped_packed_y, copy_w_even, copy_h_even);

            copy_nv12_y_plane(y_in, y_out, luma_stride_in, luma_stride_out, input_rect_even, clamped_packed_x,
                              clamped_packed_y, copy_w_even, copy_h_even, rect_index);
            copy_nv12_uv_plane(uv_in, uv_out, uv_stride_in, uv_stride_out, uv_plane_height_out, input_rect_even,
                               clamped_packed_x, clamped_packed_y, copy_w_even, copy_h_even, rect_index);
        }
    } // namespace

    /**
     * @brief Processes an NV12 frame by copying specified regions using GstVideoFrame API.
     * NV12 format has Y plane and interleaved UV plane.
     * Uses GstVideoFrame API for safe stride/memory handling with hardware buffers.
     * @param input_buffer Input buffer.
     * @param output_buffer Output buffer.
     * @param input_width Input frame width.
     * @param input_height Input frame height.
     * @param input_rectangles Input rectangles.
     * @param packed_rectangles Packed rectangles.
     */
    void process_nv12_frame(const GstBuffer *input_buffer, GstBuffer *output_buffer, const int input_width,
                            const int input_height, const std::vector<rectangle> &input_rectangles,
                            const std::vector<rectangle> &packed_rectangles) {
        const auto video_info_opt = setup_nv12_video_info(input_width, input_height, input_buffer, output_buffer);
        if (!video_info_opt) {
            return;
        }

        auto [input_info, output_info] = *video_info_opt;

        const auto frames_opt = map_nv12_frames(&input_info, &output_info, input_buffer, output_buffer);
        if (!frames_opt) {
            return;
        }

        auto [input_frame, output_frame] = *frames_opt;

        const auto planes = get_nv12_plane_data(input_frame, output_frame);
        const int max_output_x = static_cast<int>(GST_VIDEO_FRAME_WIDTH(&output_frame));
        const int max_output_y = static_cast<int>(GST_VIDEO_FRAME_HEIGHT(&output_frame));

        debug(area_copy, "NV12 frame info: input %dx%d, output %dx%d", input_width, input_height, max_output_x,
              max_output_y);

        for (size_t rect_index = 0; rect_index < packed_rectangles.size(); ++rect_index) {
            if (rect_index >= input_rectangles.size()) {
                break;
            }
            process_single_nv12_roi(planes, input_rectangles[rect_index], packed_rectangles[rect_index], max_output_x,
                                    max_output_y, rect_index);
        }

        gst_video_frame_unmap(&output_frame);
        gst_video_frame_unmap(&input_frame);
    }
} // namespace sf
