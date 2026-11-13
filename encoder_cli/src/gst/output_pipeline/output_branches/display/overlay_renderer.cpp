// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "overlay_renderer.h"

#include "../../../../../libvcai/include/logging.h"
#include "../../../../../libvcai/include/nvosd_overlay.h"
#include "../../output_pipeline_debug.h"

#include <cstdint>
#include <gst/video/video.h>

/**
 * @file overlay_renderer.cpp
 * @brief Implementation of display overlay compositing.
 */

std::optional<std::pair<int, int>> overlay_frame_dimensions(GstPad *pad, GstBuffer *buffer, NvDsBatchMeta *batch_meta) {
    const GstVideoMeta *video_meta = gst_buffer_get_video_meta(buffer);
    if (video_meta != nullptr && video_meta->width > 0 && video_meta->height > 0) {
        return std::make_pair(static_cast<int>(video_meta->width), static_cast<int>(video_meta->height));
    }

    if (batch_meta != nullptr && batch_meta->frame_meta_list != nullptr) {
        auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(batch_meta->frame_meta_list->data);
        if (frame_meta != nullptr && frame_meta->source_frame_width > 0 && frame_meta->source_frame_height > 0) {
            return std::make_pair(static_cast<int>(frame_meta->source_frame_width),
                                  static_cast<int>(frame_meta->source_frame_height));
        }
    }

    if (pad != nullptr) {
        sf::unique_gst_caps_ptr caps(gst_pad_get_current_caps(pad));
        if (caps) {
            GstVideoInfo video_info;
            gst_video_info_init(&video_info);
            if (gst_video_info_from_caps(&video_info, caps.get())) {
                const int width = GST_VIDEO_INFO_WIDTH(&video_info);
                const int height = GST_VIDEO_INFO_HEIGHT(&video_info);
                if (width > 0 && height > 0) {
                    return std::make_pair(width, height);
                }
            }
        }
    }

    return std::nullopt;
}

/**
 * @brief Clears stale display metas for a new-or-same frame on the display overlay.
 *
 * Jetson (ARM64) and x86 differ: the nvstreammux->nvosd pipeline on Jetson can cache display metas, so stale
 * overlays must also be cleared when the metas are empty; x86 only clears on an actual frame change.
 */
void clear_stale_display_metas(global_data &data, NvDsFrameMeta *frame_meta, const guint64 current_frame_num,
                               const bool is_new_frame) {
    auto &rt = data.output_display_runtime;
    if (frame_meta != nullptr && frame_meta->display_meta_list != nullptr) {
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
        bool should_clear = false;
        if (is_new_frame) {
            should_clear = true;
            rt.overlay_last_frame_num = current_frame_num;
        } else {
            NvDsMetaList *list = frame_meta->display_meta_list;
            bool all_empty = true;
            while (list != nullptr) {
                auto *display_meta = reinterpret_cast<NvDsDisplayMeta *>(list->data);
                if (display_meta != nullptr && (display_meta->num_labels > 0 || display_meta->num_rects > 0)) {
                    all_empty = false;
                    break;
                }
                list = list->next;
            }
            if (all_empty) {
                should_clear = true;
            }
        }
        if (should_clear) {
            frame_meta->display_meta_list = nullptr;
        }
#else
        if (is_new_frame) {
            frame_meta->display_meta_list = nullptr;
            rt.overlay_last_frame_num = current_frame_num;
        }
#endif
    } else if (is_new_frame) {
        rt.overlay_last_frame_num = current_frame_num;
    }
}

std::optional<overlay_frame_context> prepare_overlay_frame(GstPad *pad, GstBuffer *buffer, global_data &data,
                                                           bool &warned_missing_batch_meta,
                                                           bool &warned_create_meta_failed) {
    auto &rt = data.output_display_runtime;

    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    if (batch_meta == nullptr) {
        if (!warned_missing_batch_meta) {
            sf::warning(output_pipeline, "Display overlays: NvDsBatchMeta missing on buffer, creating minimal meta");
            warned_missing_batch_meta = true;
        }
    }
    const auto dimensions_opt = overlay_frame_dimensions(pad, buffer, batch_meta);
    if (!dimensions_opt.has_value()) {
        return std::nullopt;
    }

    const auto [width, height] = dimensions_opt.value();
    batch_meta = sf::ensure_nvds_batch_meta(buffer, width, height);
    if (batch_meta == nullptr) {
        if (!warned_create_meta_failed) {
            sf::warning(output_pipeline, "Display overlays: failed to attach NvDsBatchMeta to buffer");
            warned_create_meta_failed = true;
        }
        return std::nullopt;
    }

    auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(batch_meta->frame_meta_list->data);
    const guint64 current_frame_num =
            (frame_meta != nullptr) ? static_cast<guint64>(frame_meta->frame_num) : UINT64_MAX;
    const bool is_new_frame = (current_frame_num != UINT64_MAX && current_frame_num != rt.overlay_last_frame_num);

    clear_stale_display_metas(data, frame_meta, current_frame_num, is_new_frame);

    return overlay_frame_context{batch_meta, width, height};
}
