// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ops/sf_pri_reconstruct_ops.h"

#include <area_copy.h>
#include <gst_raii.h>
#include <gst_wrappers.h>
#include <rectangle.h>
#include <string.h>

typedef struct {
    guint src_x;
    guint src_y;
    guint dst_x;
    guint dst_y;
    guint width;
    guint height;
} sf_copy_rect;

/** @brief Packed/target rectangle in pixels (from @c sf::packed_regions_info units + log2). */
typedef struct {
    guint packed_x;
    guint packed_y;
    guint target_x;
    guint target_y;
    guint width;
    guint height;
} sf_pri_pixel_placement;

static gboolean sf_shift_units_to_pixels(const guint value_units, const guint log2_unit_size, guint *out_pixels) {
    if (out_pixels == nullptr || log2_unit_size > 31U) {
        return FALSE;
    }
    const guint64 pixels = static_cast<guint64>(value_units) << log2_unit_size;
    if (pixels > static_cast<guint64>(G_MAXUINT)) {
        return FALSE;
    }
    *out_pixels = static_cast<guint>(pixels);
    return TRUE;
}

/** @brief Clips placement to frame bounds and fills out_rect (src/dst in packed/target coords). @return TRUE if rect
 * has positive area. */
static gboolean sf_intersect_copy_rect(const sf_pri_pixel_placement *placement, guint frame_width, guint frame_height,
                                       sf_copy_rect *out_rect) {
    guint max_width = 0;
    guint max_height = 0;

    if (placement == nullptr || out_rect == nullptr) {
        return FALSE;
    }
    if (placement->width == 0U || placement->height == 0U) {
        return FALSE;
    }
    if (placement->packed_x >= frame_width || placement->packed_y >= frame_height ||
        placement->target_x >= frame_width || placement->target_y >= frame_height) {
        return FALSE;
    }

    max_width = frame_width - placement->packed_x;
    if (frame_width - placement->target_x < max_width) {
        max_width = frame_width - placement->target_x;
    }
    max_height = frame_height - placement->packed_y;
    if (frame_height - placement->target_y < max_height) {
        max_height = frame_height - placement->target_y;
    }

    out_rect->src_x = placement->packed_x;
    out_rect->src_y = placement->packed_y;
    out_rect->dst_x = placement->target_x;
    out_rect->dst_y = placement->target_y;
    out_rect->width = placement->width;
    out_rect->height = placement->height;

    if (out_rect->width > max_width) {
        out_rect->width = max_width;
    }
    if (out_rect->height > max_height) {
        out_rect->height = max_height;
    }

    return out_rect->width > 0U && out_rect->height > 0U;
}

/** @brief Fills pixel placement from one @c sf::region or returns FALSE if fields are inconsistent. */
static gboolean sf_pri_build_pixel_placement(const sf::packed_regions_info &pri, const sf::region &reg,
                                             sf_pri_pixel_placement *out_placement) {
    if (out_placement == nullptr) {
        return FALSE;
    }
    if (reg.is_a_layer_flag.value_or(false)) {
        return FALSE;
    }
    if (!reg.top_left_in_units_x.has_value() || !reg.top_left_in_units_y.has_value() ||
        !reg.width_in_units_minus1.has_value() || !reg.height_in_units_minus1.has_value()) {
        return FALSE;
    }

    const guint log2_unit_size = static_cast<guint>(pri.log2_unit_size.value_or(0U));
    const bool target_present = pri.target_pic_params_present_flag.value_or(false);

    const guint packed_x_units = static_cast<guint>(*reg.top_left_in_units_x);
    const guint packed_y_units = static_cast<guint>(*reg.top_left_in_units_y);
    const guint width_units_minus1 = static_cast<guint>(*reg.width_in_units_minus1);
    const guint height_units_minus1 = static_cast<guint>(*reg.height_in_units_minus1);
    const guint width_units = width_units_minus1 + 1U;
    const guint height_units = height_units_minus1 + 1U;

    guint target_x_units = packed_x_units;
    guint target_y_units = packed_y_units;
    if (target_present) {
        if (!reg.target_region_top_left_in_units_x.has_value() || !reg.target_region_top_left_in_units_y.has_value()) {
            return FALSE;
        }
        target_x_units = static_cast<guint>(*reg.target_region_top_left_in_units_x);
        target_y_units = static_cast<guint>(*reg.target_region_top_left_in_units_y);
    }

    if (!sf_shift_units_to_pixels(packed_x_units, log2_unit_size, &out_placement->packed_x) ||
        !sf_shift_units_to_pixels(packed_y_units, log2_unit_size, &out_placement->packed_y) ||
        !sf_shift_units_to_pixels(width_units, log2_unit_size, &out_placement->width) ||
        !sf_shift_units_to_pixels(height_units, log2_unit_size, &out_placement->height) ||
        !sf_shift_units_to_pixels(target_x_units, log2_unit_size, &out_placement->target_x) ||
        !sf_shift_units_to_pixels(target_y_units, log2_unit_size, &out_placement->target_y)) {
        return FALSE;
    }
    return TRUE;
}

/** @brief Copies rows of width_bytes from src to dst (stride-based). */
static void sf_copy_rows(const guint8 *src, guint src_stride, guint8 *dst, guint dst_stride, guint width_bytes,
                         guint rows) {
    for (guint row = 0; row < rows; ++row) {
        memcpy(dst + (static_cast<gsize>(row) * dst_stride), src + (static_cast<gsize>(row) * src_stride), width_bytes);
    }
}

/** @brief Copies rect from src_data (I420 layout) into frame Y/U/V planes with chroma subsampling. */
static void sf_copy_i420_region(const guint8 *src_data, GstVideoFrame *frame, const GstVideoInfo *video_info,
                                const sf_copy_rect *rect) {
    guint8 *dst_y = nullptr;
    guint8 *dst_u = nullptr;
    guint8 *dst_v = nullptr;
    const guint8 *src_y = nullptr;
    const guint8 *src_u = nullptr;
    const guint8 *src_v = nullptr;
    guint y_stride = 0;
    guint u_stride = 0;
    guint v_stride = 0;

    dst_y = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));
    dst_u = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 1));
    dst_v = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 2));
    y_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0));
    u_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 1));
    v_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 2));
    src_y = src_data + GST_VIDEO_INFO_PLANE_OFFSET(video_info, 0);
    src_u = src_data + GST_VIDEO_INFO_PLANE_OFFSET(video_info, 1);
    src_v = src_data + GST_VIDEO_INFO_PLANE_OFFSET(video_info, 2);

    sf_copy_rows(src_y + (static_cast<gsize>(rect->src_y) * y_stride) + rect->src_x, y_stride,
                 dst_y + (static_cast<gsize>(rect->dst_y) * y_stride) + rect->dst_x, y_stride, rect->width,
                 rect->height);

    if (rect->width >= 2U && rect->height >= 2U) {
        const guint chroma_src_x = rect->src_x / 2U;
        const guint chroma_src_y = rect->src_y / 2U;
        const guint chroma_dst_x = rect->dst_x / 2U;
        const guint chroma_dst_y = rect->dst_y / 2U;
        const guint chroma_width = rect->width / 2U;
        const guint chroma_height = rect->height / 2U;

        sf_copy_rows(src_u + (static_cast<gsize>(chroma_src_y) * u_stride) + chroma_src_x, u_stride,
                     dst_u + (static_cast<gsize>(chroma_dst_y) * u_stride) + chroma_dst_x, u_stride, chroma_width,
                     chroma_height);
        sf_copy_rows(src_v + (static_cast<gsize>(chroma_src_y) * v_stride) + chroma_src_x, v_stride,
                     dst_v + (static_cast<gsize>(chroma_dst_y) * v_stride) + chroma_dst_x, v_stride, chroma_width,
                     chroma_height);
    }
}

/** @brief Copies rect from src_data (NV12 layout) into frame Y/UV planes. */
static void sf_copy_nv12_region(const guint8 *src_data, GstVideoFrame *frame, const GstVideoInfo *video_info,
                                const sf_copy_rect *rect) {
    guint8 *dst_y = nullptr;
    guint8 *dst_uv = nullptr;
    const guint8 *src_y = nullptr;
    const guint8 *src_uv = nullptr;
    guint y_stride = 0;
    guint uv_stride = 0;

    dst_y = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));
    dst_uv = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(frame, 1));
    y_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0));
    uv_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 1));
    src_y = src_data + GST_VIDEO_INFO_PLANE_OFFSET(video_info, 0);
    src_uv = src_data + GST_VIDEO_INFO_PLANE_OFFSET(video_info, 1);

    sf_copy_rows(src_y + (static_cast<gsize>(rect->src_y) * y_stride) + rect->src_x, y_stride,
                 dst_y + (static_cast<gsize>(rect->dst_y) * y_stride) + rect->dst_x, y_stride, rect->width,
                 rect->height);

    if (rect->width >= 2U && rect->height >= 2U) {
        const guint chroma_src_x = rect->src_x;
        const guint chroma_src_y = rect->src_y / 2U;
        const guint chroma_dst_x = rect->dst_x;
        const guint chroma_dst_y = rect->dst_y / 2U;
        const guint chroma_width_bytes = rect->width;
        const guint chroma_height = rect->height / 2U;

        sf_copy_rows(src_uv + (static_cast<gsize>(chroma_src_y) * uv_stride) + chroma_src_x, uv_stride,
                     dst_uv + (static_cast<gsize>(chroma_dst_y) * uv_stride) + chroma_dst_x, uv_stride,
                     chroma_width_bytes, chroma_height);
    }
}

/** @brief Aligns rect coordinates and size to even for 4:2:0 using libvcai (bounds-safe). */
static void sf_enforce_even_for_subsampled(sf_copy_rect *rect, guint frame_width, guint frame_height) {
    sf::rectangle packed_rect(static_cast<int>(rect->src_x), static_cast<int>(rect->src_y),
                              static_cast<int>(rect->width), static_cast<int>(rect->height));
    packed_rect =
            sf::ensure_even_dimensions(packed_rect, static_cast<int>(frame_width), static_cast<int>(frame_height));
    rect->src_x = static_cast<guint>(packed_rect.left);
    rect->src_y = static_cast<guint>(packed_rect.top);
    rect->width = static_cast<guint>(packed_rect.width);
    rect->height = static_cast<guint>(packed_rect.height);
    rect->dst_x = rect->dst_x & ~1U;
    rect->dst_y = rect->dst_y & ~1U;
}

/** @brief Fills a plane with a constant byte value. */
static void sf_fill_plane(guint8 *plane_data, const guint stride, const guint width, const guint height,
                          const guint8 value) {
    for (guint row = 0U; row < height; ++row) {
        memset(plane_data + (static_cast<gsize>(row) * stride), value, width);
    }
}

/** @brief Fills I420 frame with luma/chroma background values. */
static void sf_fill_i420_background(GstVideoFrame *output_frame, const GstVideoInfo *video_info,
                                    const guint32 background_color) {
    guint8 *output_y = nullptr;
    guint8 *output_u = nullptr;
    guint8 *output_v = nullptr;
    const guint y_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0));
    const guint u_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 1));
    const guint v_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 2));
    const guint y_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 0);
    const guint y_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 0);
    const guint u_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 1);
    const guint u_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 1);
    const guint v_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 2);
    const guint v_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 2);
    guint8 luma_value = 0x00U;
    guint8 chroma_value = 0x00U;

    if (background_color != 0U) {
        luma_value = 0x00U;
        chroma_value = 0x80U;
    }

    output_y = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(output_frame, 0));
    output_u = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(output_frame, 1));
    output_v = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(output_frame, 2));
    sf_fill_plane(output_y, y_stride, y_width, y_height, luma_value);
    sf_fill_plane(output_u, u_stride, u_width, u_height, chroma_value);
    sf_fill_plane(output_v, v_stride, v_width, v_height, chroma_value);
}

/** @brief Fills NV12 frame with luma/UV background values. */
static void sf_fill_nv12_background(GstVideoFrame *output_frame, const GstVideoInfo *video_info,
                                    const guint32 background_color) {
    guint8 *output_y = nullptr;
    guint8 *output_uv = nullptr;
    const guint y_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 0));
    const guint uv_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(video_info, 1));
    const guint y_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 0);
    const guint y_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 0);
    const guint uv_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 1);
    const guint uv_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 1);
    guint8 luma_value = 0x00U;
    guint8 uv_value = 0x00U;

    if (background_color != 0U) {
        luma_value = 0x00U;
        uv_value = 0x80U;
    }

    output_y = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(output_frame, 0));
    output_uv = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(output_frame, 1));
    sf_fill_plane(output_y, y_stride, y_width, y_height, luma_value);
    sf_fill_plane(output_uv, uv_stride, uv_width, uv_height, uv_value);
}

/** @brief Fills output frame with background; dispatches by format (I420/NV12). @return TRUE if format supported. */
static gboolean sf_fill_output_background(GstVideoFrame *output_frame, const GstVideoInfo *video_info,
                                          const guint32 background_color) {
    const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(video_info);
    if (format == GST_VIDEO_FORMAT_I420) {
        sf_fill_i420_background(output_frame, video_info, background_color);
        return TRUE;
    }
    if (format == GST_VIDEO_FORMAT_NV12) {
        sf_fill_nv12_background(output_frame, video_info, background_color);
        return TRUE;
    }
    return FALSE;
}

/** @brief Copies each PRI region from input to output frame (I420 or NV12), updates out_stats. @return TRUE on success.
 */
static gboolean sf_apply_pri_regions_to_output(const guint8 *input_data, GstVideoFrame *output_frame,
                                               const GstVideoInfo *video_info, const sf::packed_regions_info &pri,
                                               sf_pri_reconstruct_stats *out_stats) {
    sf_pri_reconstruct_stats stats = {0};
    const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(video_info);

    if (input_data == nullptr || output_frame == nullptr || video_info == nullptr) {
        return FALSE;
    }
    if (!sf::is_valid_video_dimensions(static_cast<int>(video_info->width), static_cast<int>(video_info->height))) {
        return FALSE;
    }

    if (pri.cancel_flag) {
        if (out_stats != nullptr) {
            *out_stats = stats;
        }
        return TRUE;
    }

    const bool target_present = pri.target_pic_params_present_flag.value_or(false);
    if (target_present && (!pri.target_pic_width_minus1.has_value() || !pri.target_pic_height_minus1.has_value())) {
        return FALSE;
    }

    for (const sf::region &reg: pri.regions) {
        sf_pri_pixel_placement placement = {0};
        sf_copy_rect rect = {0};

        if (reg.is_a_layer_flag.value_or(false)) {
            continue;
        }
        if (!sf_pri_build_pixel_placement(pri, reg, &placement)) {
            return FALSE;
        }

        if (!sf_intersect_copy_rect(&placement, video_info->width, video_info->height, &rect)) {
            ++stats.skipped_regions;
            continue;
        }

        /* Document 4:2:0 even-dimension contract via libvcai (no behavior change before ensure_even). */
        {
            const sf::rectangle intersected(static_cast<int>(rect.src_x), static_cast<int>(rect.src_y),
                                            static_cast<int>(rect.width), static_cast<int>(rect.height));
            [[maybe_unused]] const bool packed_even = sf::has_even_dimensions(intersected);
            (void) packed_even;
        }

        if (format == GST_VIDEO_FORMAT_I420 || format == GST_VIDEO_FORMAT_NV12) {
            sf_enforce_even_for_subsampled(&rect, video_info->width, video_info->height);
        }
        if (rect.width == 0U || rect.height == 0U) {
            ++stats.skipped_regions;
            continue;
        }

        if (format == GST_VIDEO_FORMAT_I420) {
            sf_copy_i420_region(input_data, output_frame, video_info, &rect);
            ++stats.copied_regions;
        } else if (format == GST_VIDEO_FORMAT_NV12) {
            sf_copy_nv12_region(input_data, output_frame, video_info, &rect);
            ++stats.copied_regions;
        } else {
            ++stats.skipped_regions;
            break;
        }
    }

    if (out_stats != nullptr) {
        *out_stats = stats;
    }
    return TRUE;
}

gboolean sf_pri_reconstruct_in_place(GstBuffer *buffer, const GstVideoInfo *video_info,
                                     const sf::packed_regions_info &pri, sf_pri_reconstruct_stats *out_stats) {
    GstVideoFrame frame = {0};
    guint8 *src_copy = nullptr;
    GstMapInfo src_map = {0};
    gboolean apply_ok = FALSE;

    if (buffer == nullptr || video_info == nullptr) {
        return FALSE;
    }

    if (!gst_buffer_map(buffer, &src_map, GST_MAP_READ)) {
        return FALSE;
    }
    {
        const sf::unique_gst_buffer_unmapper unmap_guard(buffer, &src_map);
        src_copy = static_cast<guint8 *>(g_memdup2(src_map.data, src_map.size));
    }
    if (src_copy == nullptr) {
        return FALSE;
    }

    if (!gst_video_frame_map(&frame, video_info, buffer, GST_MAP_READWRITE)) {
        g_free(src_copy);
        return FALSE;
    }

    apply_ok = sf_apply_pri_regions_to_output(src_copy, &frame, video_info, pri, out_stats);
    gst_video_frame_unmap(&frame);
    g_free(src_copy);
    return apply_ok;
}

gboolean sf_pri_reconstruct_to_output(GstBuffer *input_buffer, GstBuffer *output_buffer, const GstVideoInfo *video_info,
                                      const sf::packed_regions_info &pri, const guint32 background_color,
                                      sf_pri_reconstruct_stats *out_stats) {
    GstMapInfo input_map = {0};
    GstVideoFrame output_frame = {0};
    gboolean fill_ok = FALSE;
    gboolean apply_ok = FALSE;

    if (input_buffer == nullptr || output_buffer == nullptr || video_info == nullptr) {
        return FALSE;
    }
    if (!gst_buffer_map(input_buffer, &input_map, GST_MAP_READ)) {
        return FALSE;
    }
    const sf::unique_gst_buffer_unmapper unmap_in(input_buffer, &input_map);
    if (!gst_video_frame_map(&output_frame, video_info, output_buffer, GST_MAP_WRITE)) {
        return FALSE;
    }

    fill_ok = sf_fill_output_background(&output_frame, video_info, background_color);
    if (fill_ok) {
        apply_ok = sf_apply_pri_regions_to_output(input_map.data, &output_frame, video_info, pri, out_stats);
    }

    gst_video_frame_unmap(&output_frame);
    return fill_ok && apply_ok;
}
