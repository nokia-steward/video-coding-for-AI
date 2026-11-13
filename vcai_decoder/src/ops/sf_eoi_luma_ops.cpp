// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ops/sf_eoi_luma_ops.h"

#include <gst_wrappers.h>
#include <luma_adaptation_math.h>
#include <math.h>

gboolean sf_compute_eoi_luma_multiplier(const sf_eoi_payload_info *eoi, gdouble *out_multiplier) {
    if (eoi == nullptr || out_multiplier == nullptr) {
        return FALSE;
    }
    if (!eoi->parse_ok || !eoi->present || eoi->cancel_flag || !eoi->has_luma_adaptation) {
        return FALSE;
    }
    if (eoi->luma_adaptation_idc != 1U) {
        return FALSE;
    }

    const int ratio_percentage = sf::derive_luma_ratio_percentage(eoi->ratio_luma_sign_flag != FALSE,
                                                                  static_cast<uint8_t>(eoi->ratio_luma_value));
    const std::optional<double> multiplier =
            sf::compute_luma_backscale_multiplier(ratio_percentage, static_cast<int>(eoi->backscale_ratio_value));
    if (!multiplier.has_value()) {
        return FALSE;
    }
    *out_multiplier = static_cast<gdouble>(*multiplier);
    return TRUE;
}

gboolean sf_apply_luma_multiplier_in_place(GstBuffer *buffer, const GstVideoInfo *video_info,
                                           const gdouble multiplier) {
    GstVideoFrame frame = {0};
    guint8 *luma_plane = nullptr;
    gint luma_height = 0;
    gint luma_width = 0;
    gint luma_stride = 0;
    gint luma_pixel_stride = 0;

    if (buffer == nullptr || video_info == nullptr || multiplier <= 0.0) {
        return FALSE;
    }
    if (!sf::is_valid_video_dimensions(static_cast<int>(GST_VIDEO_INFO_WIDTH(video_info)),
                                       static_cast<int>(GST_VIDEO_INFO_HEIGHT(video_info)))) {
        return FALSE;
    }
    if (!gst_video_frame_map(&frame, video_info, buffer, GST_MAP_READWRITE)) {
        return FALSE;
    }

    luma_plane = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    if (luma_plane == nullptr) {
        gst_video_frame_unmap(&frame);
        return FALSE;
    }

    luma_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 0);
    luma_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 0);
    luma_stride = GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0);
    luma_pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(video_info, 0);

    for (gint row = 0; row < luma_height; ++row) {
        guint8 *row_ptr = luma_plane + (static_cast<gsize>(row) * static_cast<gsize>(luma_stride));
        for (gint col = 0; col < luma_width; ++col) {
            const gsize index = static_cast<gsize>(col) * static_cast<gsize>(luma_pixel_stride);
            const gdouble original = static_cast<gdouble>(row_ptr[index]);
            const gint scaled = static_cast<gint>(floor(original * multiplier));
            row_ptr[index] = static_cast<guint8>(CLAMP(scaled, 0, 255));
        }
    }

    gst_video_frame_unmap(&frame);
    return TRUE;
}
