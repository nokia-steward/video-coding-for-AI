// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../include/nvosd_overlay.h"

#include <algorithm>
#include <gstnvdsmeta.h>
#include <nvdsmeta.h>
#include <nvdsmeta_schema.h>

namespace {
    /**
     * @brief Get the first frame meta from a batch.
     * @param batch_meta Batch metadata to inspect.
     * @return Pointer to the first frame meta or nullptr when missing.
     */
    [[nodiscard]] NvDsFrameMeta *get_first_frame_meta(NvDsBatchMeta *batch_meta) {
        if (batch_meta == nullptr || batch_meta->frame_meta_list == nullptr) {
            return nullptr;
        }

        return reinterpret_cast<NvDsFrameMeta *>(batch_meta->frame_meta_list->data);
    }

    /**
     * @brief Finds an existing display meta on the frame that has space for additional labels.
     * Only reuses display metas that already have labels (created by another probe on the same frame).
     * @param frame_meta The frame meta to search.
     * @return Pointer to display meta with space, or nullptr if none found.
     */
    [[nodiscard]] NvDsDisplayMeta *find_display_meta_with_label_space(NvDsFrameMeta *frame_meta) {
        if (frame_meta == nullptr) {
            return nullptr;
        }

        // Only reuse if there's exactly one display meta with labels (created by another probe on same frame)
        // If there are multiple or it's empty, don't reuse to avoid stale data from previous frames
        NvDsMetaList *l = frame_meta->display_meta_list;
        NvDsDisplayMeta *candidate = nullptr;
        int display_meta_count = 0;

        while (l != nullptr) {
            auto *display_meta = reinterpret_cast<NvDsDisplayMeta *>(l->data);
            if (display_meta != nullptr) {
                display_meta_count++;
                if (display_meta->num_labels < MAX_LABEL_SIZE && display_meta->num_labels > 0) {
                    // Only reuse if it already has labels (meaning another probe added to it this frame)
                    candidate = display_meta;
                }
            }
            l = l->next;
        }

        // Only reuse if there's exactly one display meta that already has labels
        // This means another probe on the same frame added to it
        if (display_meta_count == 1 && candidate != nullptr) {
            return candidate;
        }

        return nullptr;
    }

    /**
     * @brief Finds an existing display meta on the frame that has space for additional rects.
     * Only reuses display metas that already have rects (created by another probe on the same frame).
     * @param frame_meta The frame meta to search.
     * @return Pointer to display meta with space, or nullptr if none found.
     */
    [[nodiscard]] NvDsDisplayMeta *find_display_meta_with_rect_space(NvDsFrameMeta *frame_meta) {
        if (frame_meta == nullptr) {
            return nullptr;
        }

        // Only reuse if there's exactly one display meta with rects (created by another probe on same frame)
        // If there are multiple or it's empty, don't reuse to avoid stale data from previous frames
        NvDsMetaList *l = frame_meta->display_meta_list;
        NvDsDisplayMeta *candidate = nullptr;
        int display_meta_count = 0;

        while (l != nullptr) {
            auto *display_meta = reinterpret_cast<NvDsDisplayMeta *>(l->data);
            if (display_meta != nullptr) {
                display_meta_count++;
                if (display_meta->num_rects < MAX_ELEMENTS_IN_DISPLAY_META && display_meta->num_rects > 0) {
                    // Only reuse if it already has rects (meaning another probe added to it this frame)
                    candidate = display_meta;
                }
            }
            l = l->next;
        }

        // Only reuse if there's exactly one display meta that already has rects
        // This means another probe on the same frame added to it
        if (display_meta_count == 1 && candidate != nullptr) {
            return candidate;
        }

        return nullptr;
    }

    /**
     * @brief Convert a YUV color enum to NvOSD RGBA parameters.
     * @param color YUV color selection.
     * @return NvOSD color parameters for overlay rendering.
     */
    [[nodiscard]] NvOSD_ColorParams to_nvosd_color(const sf::yuv_color color) {
        NvOSD_ColorParams color_params{};

        switch (color) {
            case sf::yuv_color::blue:
                color_params = {0.0F, 0.0F, 1.0F, 1.0F};
                break;
            case sf::yuv_color::purple:
                color_params = {0.5F, 0.0F, 0.5F, 1.0F};
                break;
            case sf::yuv_color::red:
                color_params = {1.0F, 0.0F, 0.0F, 1.0F};
                break;
            case sf::yuv_color::yellow_orange:
                color_params = {1.0F, 0.6F, 0.0F, 1.0F};
                break;
            case sf::yuv_color::white:
                color_params = {1.0F, 1.0F, 1.0F, 1.0F};
                break;
            case sf::yuv_color::dark_gray:
                color_params = {0.2F, 0.2F, 0.2F, 1.0F};
                break;
            default:
                color_params = {1.0F, 1.0F, 1.0F, 1.0F};
                break;
        }

        return color_params;
    }
} // namespace

namespace sf {
    NvDsBatchMeta *ensure_nvds_batch_meta(GstBuffer *buffer, const int width, const int height) {
        if (buffer == nullptr || width <= 0 || height <= 0) {
            return nullptr;
        }

        auto *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
        if (batch_meta != nullptr) {
            return batch_meta;
        }

        batch_meta = nvds_create_batch_meta(1);
        if (batch_meta == nullptr) {
            return nullptr;
        }

        auto *frame_meta = nvds_acquire_frame_meta_from_pool(batch_meta);
        if (frame_meta == nullptr) {
            nvds_destroy_batch_meta(batch_meta);
            return nullptr;
        }

        frame_meta->pad_index = 0;
        frame_meta->batch_id = 0;
        frame_meta->frame_num = 0;
        frame_meta->source_id = 0;
        frame_meta->num_surfaces_per_frame = 1;
        frame_meta->source_frame_width = static_cast<guint>(width);
        frame_meta->source_frame_height = static_cast<guint>(height);
        frame_meta->pipeline_width = static_cast<guint>(width);
        frame_meta->pipeline_height = static_cast<guint>(height);
        frame_meta->buf_pts = GST_BUFFER_PTS(buffer);

        nvds_add_frame_meta_to_batch(batch_meta, frame_meta);

        if (gst_buffer_add_nvds_meta(buffer, batch_meta, nullptr, nvds_batch_meta_copy_func,
                                     nvds_batch_meta_release_func) == nullptr) {
            nvds_destroy_batch_meta(batch_meta);
            return nullptr;
        }

        return batch_meta;
    }

    bool add_rect_to_display_meta(NvDsBatchMeta *batch_meta, const sf::rectangle &rect, const sf::yuv_color color,
                                  const int border_width) {
        auto *frame_meta = get_first_frame_meta(batch_meta);
        if (frame_meta == nullptr) {
            return false;
        }

        nvds_acquire_meta_lock(batch_meta);
        NvDsDisplayMeta *display_meta = find_display_meta_with_rect_space(frame_meta);
        const bool reuse_existing = (display_meta != nullptr);

        if (!reuse_existing) {
            display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
            if (display_meta == nullptr) {
                nvds_release_meta_lock(batch_meta);
                return false;
            }
            display_meta->num_rects = 0;
            display_meta->num_labels = 0;
            display_meta->num_lines = 0;
            display_meta->num_arrows = 0;
            display_meta->num_circles = 0;
        }

        const guint rect_index = display_meta->num_rects;
        if (rect_index >= MAX_ELEMENTS_IN_DISPLAY_META) {
            nvds_release_meta_lock(batch_meta);
            return false;
        }

        auto &rect_params = display_meta->rect_params[rect_index];
        rect_params.left = static_cast<float>(rect.left);
        rect_params.top = static_cast<float>(rect.top);
        rect_params.width = static_cast<float>(rect.width);
        rect_params.height = static_cast<float>(rect.height);
        rect_params.border_width = static_cast<guint>(std::max(border_width, 1));
        rect_params.border_color = to_nvosd_color(color);
        rect_params.has_bg_color = 0;
        display_meta->num_rects = rect_index + 1;

        if (!reuse_existing) {
            nvds_add_display_meta_to_frame(frame_meta, display_meta);
        }
        nvds_release_meta_lock(batch_meta);
        return true;
    }

    bool add_text_to_display_meta(NvDsBatchMeta *batch_meta, const char *text, const int x, const int y,
                                  const sf::yuv_color color, const int font_size) {
        if (text == nullptr || text[0] == '\0') {
            return false;
        }

        auto *frame_meta = get_first_frame_meta(batch_meta);
        if (frame_meta == nullptr) {
            return false;
        }

        nvds_acquire_meta_lock(batch_meta);
        NvDsDisplayMeta *display_meta = find_display_meta_with_label_space(frame_meta);
        const bool reuse_existing = (display_meta != nullptr);

        if (!reuse_existing) {
            display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
            if (display_meta == nullptr) {
                nvds_release_meta_lock(batch_meta);
                return false;
            }
            display_meta->num_labels = 0;
            display_meta->num_rects = 0;
            display_meta->num_lines = 0;
            display_meta->num_arrows = 0;
            display_meta->num_circles = 0;
        }

        const guint label_index = display_meta->num_labels;
        if (label_index >= MAX_LABEL_SIZE) {
            nvds_release_meta_lock(batch_meta);
            return false;
        }

        auto &text_params = display_meta->text_params[label_index];
        text_params.display_text = g_strdup(text);
        text_params.x_offset = static_cast<guint>(std::max(x, 0));
        text_params.y_offset = static_cast<guint>(std::max(y, 0));

        auto &font_params = text_params.font_params;
        // NvOSD expects a mutable font name pointer; use a static literal to avoid allocation.
        font_params.font_name = const_cast<char *>("Sans");
        font_params.font_size = static_cast<guint>(std::max(font_size, 1));
        font_params.font_color = to_nvosd_color(color);

        text_params.set_bg_clr = 1;
        text_params.text_bg_clr = to_nvosd_color(sf::yuv_color::dark_gray);
        display_meta->num_labels = label_index + 1;

        if (!reuse_existing) {
            nvds_add_display_meta_to_frame(frame_meta, display_meta);
        }
        nvds_release_meta_lock(batch_meta);
        return true;
    }
} // namespace sf
