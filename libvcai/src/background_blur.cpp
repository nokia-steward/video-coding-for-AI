// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../include/background_blur.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include "../include/gst_raii.h"
#include "../include/logging.h"
#include "background_blur/internal.h"

GST_DEBUG_CATEGORY_STATIC(background_blur);
#define GST_CAT_DEFAULT background_blur

static bool debug_category_initialized = [] {
    GST_DEBUG_CATEGORY_INIT(background_blur, "background-blur", 0, "Background blur utilities");
    return true;
}();

void init_background_blur_debug_category() {
    GST_DEBUG_CATEGORY_INIT(background_blur, "background-blur", 0, "Background blur utilities");
}

namespace sf {
    bool apply_background_blur(const unique_gst_buffer_no_delete_ptr &buffer, const std::vector<rectangle> &rois,
                               const double sigma, const int width, const int height, video_format format,
                               const int kernel_size) {
        if (!buffer || width <= 0 || height <= 0 || sigma <= 0.0) {
            error(background_blur, "Invalid parameters for background blur");
            return false;
        }

        const int actual_kernel_size =
                (kernel_size > 0) ? kernel_size : background_blur_internal::calculate_kernel_size(sigma);
        if (actual_kernel_size % 2 == 0) {
            error(background_blur, "Kernel size must be odd, got %d", actual_kernel_size);
            return false;
        }

        const std::vector<double> kernel =
                background_blur_internal::generate_gaussian_kernel(actual_kernel_size, sigma);

        const char *format_str = video_format_to_string(format);
        const unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format_str, "width",
                                                           G_TYPE_INT, width, "height", G_TYPE_INT, height, nullptr));
        if (!caps) {
            error(background_blur, "Failed to create caps for background blur");
            return false;
        }

        GstVideoInfo video_info;
        if (!gst_video_info_from_caps(&video_info, caps.get())) {
            error(background_blur, "Failed to parse video info from caps");
            return false;
        }

        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &video_info, buffer.get(), GST_MAP_WRITE)) {
            error(background_blur, "Failed to map frame for writing");
            return false;
        }

        bool success = false;
        const GstVideoFormat gst_video_format = video_info.finfo->format;
        if (gst_video_format == GST_VIDEO_FORMAT_NV12) {
            success =
                    background_blur_internal::blur_nv12_frame(&frame, kernel, actual_kernel_size, width, height, rois);
        } else if (gst_video_format == GST_VIDEO_FORMAT_I420) {
            success = background_blur_internal::blur_yuv420_frame(&frame, kernel, actual_kernel_size, width, height,
                                                                  rois);
        } else {
            error(background_blur, "Unsupported video format: %s", gst_video_format_to_string(gst_video_format));
            success = false;
        }

        gst_video_frame_unmap(&frame);

        if (success) {
            debug(background_blur, "Applied background blur with sigma=%.2f, kernel_size=%d to %zu ROIs", sigma,
                  actual_kernel_size, rois.size());
        }

        return success;
    }
} // namespace sf
