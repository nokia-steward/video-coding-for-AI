#include "overlay_probes.h"

#include <cstring>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <gst/video/video.h>
#include <gstnvdsmeta.h>

#include "../../pipeline.h"
#include "logging.h"
#include "nvosd_overlay.h"
#include "text_drawer.h"

GST_DEBUG_CATEGORY_EXTERN(display_branch_category);

namespace {
    [[nodiscard]] GstBuffer *prepare_writable_buffer(GstPadProbeInfo *info, rtp_pipeline *pipeline) {
        auto *buffer_raw = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer_raw) {
            return nullptr;
        }

        auto *buffer = gst_buffer_make_writable(buffer_raw);
        if (!buffer) {
            sf::debug(display_branch_category, "Failed to make buffer writable for overlays on port %u",
                      pipeline->get_port());
            return nullptr;
        }

        if (buffer != buffer_raw) {
            GstBuffer *new_buffer = buffer;
            std::memcpy(&info->data, &new_buffer, sizeof(GstBuffer *));
            ++pipeline->stats_context.statistics.writable_buffer_conversions;
        }

        return buffer;
    }

    [[nodiscard]] std::optional<std::pair<int, int>> get_frame_dimensions(GstPad *pad, GstBuffer *buffer,
                                                                          NvDsBatchMeta *batch_meta) {
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
            if (GstCaps *caps = gst_pad_get_current_caps(pad)) {
                GstVideoInfo video_info;
                gst_video_info_init(&video_info);
                if (gst_video_info_from_caps(&video_info, caps)) {
                    const int width = GST_VIDEO_INFO_WIDTH(&video_info);
                    const int height = GST_VIDEO_INFO_HEIGHT(&video_info);
                    gst_caps_unref(caps);
                    if (width > 0 && height > 0) {
                        return std::make_pair(width, height);
                    }
                } else {
                    gst_caps_unref(caps);
                }
            }
        }

        return std::nullopt;
    }

    void clear_display_meta_if_needed(NvDsBatchMeta *batch_meta, rtp_pipeline *pipeline) {
        if (pipeline == nullptr) {
            return;
        }
        if (batch_meta == nullptr || batch_meta->frame_meta_list == nullptr) {
            return;
        }

        auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(batch_meta->frame_meta_list->data);
        const guint64 current_frame_num =
                (frame_meta != nullptr) ? static_cast<guint64>(frame_meta->frame_num) : UINT64_MAX;
        const bool is_new_frame = (current_frame_num != UINT64_MAX &&
                                   current_frame_num != pipeline->stats_context.overlay_state.last_frame_num);

        if (frame_meta != nullptr && frame_meta->display_meta_list != nullptr) {
            if (is_new_frame) {
                frame_meta->display_meta_list = nullptr;
                pipeline->stats_context.overlay_state.last_frame_num = current_frame_num;
            }
        } else if (is_new_frame) {
            pipeline->stats_context.overlay_state.last_frame_num = current_frame_num;
        }
    }

    [[nodiscard]] std::optional<std::pair<int, int>> calculate_bitrate_text_position(size_t text_length, int width,
                                                                                     int height) {
        constexpr int padding_x = 24;
        constexpr int padding_y = 24;
        constexpr int safety_margin = 8;
        const int text_width = static_cast<int>(text_length) * (sf::TEXT_CHAR_WIDTH + 1) + safety_margin;
        const int start_x = width - text_width - padding_x;
        const int start_y = height - sf::TEXT_CHAR_HEIGHT - padding_y;

        if (start_x >= 0 && start_y >= 0 && start_y + sf::TEXT_CHAR_HEIGHT <= height) {
            return std::make_pair(start_x, start_y);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::pair<int, int>> calculate_fps_text_position(size_t text_length, int width) {
        constexpr int padding_x = 24;
        constexpr int padding_y = 24;
        constexpr int safety_margin = 8;
        const int text_width = static_cast<int>(text_length) * (sf::TEXT_CHAR_WIDTH + 1) + safety_margin;
        const int start_x = width - text_width - padding_x;
        const int start_y = padding_y;

        if (start_x >= 0) {
            return std::make_pair(start_x, start_y);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string format_fps_string(const double fps) {
        if (fps <= 0.0) {
            return {};
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << fps << " fps";
        return oss.str();
    }
} // namespace

bool add_display_overlay_probes(rtp_pipeline *pipeline) {
    if (!pipeline) {
        return false;
    }

    const bool overlays_enabled = pipeline->is_fps_overlay_enabled() || pipeline->is_bitrate_overlay_enabled() ||
                                  pipeline->is_bitrate_overlay_large_enabled();
    if (!overlays_enabled) {
        return true;
    }

    GstElement *probe_element = pipeline->nvstreammux ? pipeline->nvstreammux.get() : pipeline->nvosd_display.get();
    const char *pad_name = pipeline->nvstreammux ? "src" : "sink";
    if (!probe_element) {
        sf::warning(display_branch_category, "Display overlays requested but probe element is missing");
        return false;
    }

    GstPad *pad = gst_element_get_static_pad(probe_element, pad_name);
    if (!pad) {
        sf::warning(display_branch_category, "Failed to get pad '%s' for display probes", pad_name);
        return false;
    }

    bool success = true;
    if (pipeline->is_fps_overlay_enabled()) {
        const gulong probe_id =
                gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, display_fps_overlay_probe, pipeline, nullptr);
        if (probe_id == 0) {
            sf::warning(display_branch_category, "Failed to attach display FPS probe (%s:%s)",
                        pipeline->nvstreammux ? "streammux" : "nvosd", pad_name);
            success = false;
        } else {
            sf::info(display_branch_category, "Attached display FPS probe (%s:%s)",
                     pipeline->nvstreammux ? "streammux" : "nvosd", pad_name);
        }
    }

    if (pipeline->is_bitrate_overlay_enabled() || pipeline->is_bitrate_overlay_large_enabled()) {
        const gulong probe_id =
                gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, display_bitrate_overlay_probe, pipeline, nullptr);
        if (probe_id == 0) {
            sf::warning(display_branch_category, "Failed to attach display bitrate probe (%s:%s)",
                        pipeline->nvstreammux ? "streammux" : "nvosd", pad_name);
            success = false;
        } else {
            sf::info(display_branch_category, "Attached display bitrate probe (%s:%s)",
                     pipeline->nvstreammux ? "streammux" : "nvosd", pad_name);
        }
    }

    gst_object_unref(pad);
    return success;
}

GstPadProbeReturn display_bitrate_overlay_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    auto *pipeline = static_cast<rtp_pipeline *>(user_data);
    if (!pipeline || (!pipeline->draw_bitrate && !pipeline->draw_bitrate_large)) {
        return GST_PAD_PROBE_OK;
    }

    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *read_buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!read_buffer) {
        return GST_PAD_PROBE_OK;
    }

    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(read_buffer);
    if (batch_meta == nullptr) {
        if (!pipeline->stats_context.overlay_state.bitrate_warned_missing_meta) {
            sf::warning(display_branch_category,
                        "Display overlays: NvDsBatchMeta missing on buffer, creating minimal meta");
            pipeline->stats_context.overlay_state.bitrate_warned_missing_meta = true;
        }
    }
    const auto dimensions_opt = get_frame_dimensions(pad, read_buffer, batch_meta);
    if (!dimensions_opt.has_value()) {
        return GST_PAD_PROBE_OK;
    }

    const auto [width, height] = dimensions_opt.value();
    const GstClockTime current_time = gst_clock_get_time(gst_system_clock_obtain());
    const bool should_update =
            (pipeline->stats_context.overlay_state.bitrate_last_update_time == GST_CLOCK_TIME_NONE) ||
            (current_time - pipeline->stats_context.overlay_state.bitrate_last_update_time >= GST_SECOND);

    std::string bitrate_str;
    if (should_update) {
        bitrate_str = pipeline->stats_context.statistics.incoming_bitrate_tracker.format_bitrate_string();
        if (bitrate_str.empty()) {
            return GST_PAD_PROBE_OK;
        }
        pipeline->stats_context.overlay_state.last_bitrate_string = bitrate_str;
        pipeline->stats_context.overlay_state.bitrate_last_update_time = current_time;
    } else {
        bitrate_str = pipeline->stats_context.overlay_state.last_bitrate_string;
        if (bitrate_str.empty()) {
            return GST_PAD_PROBE_OK;
        }
    }

    std::optional<std::pair<int, int>> text_position = std::nullopt;
    bool draw_large_text = false;

    constexpr int font_size_normal = 12;
    constexpr int font_size_large = 24;

    if (pipeline->draw_bitrate_large) {
        constexpr int padding_x = 32;
        constexpr int padding_y = 32;
        constexpr int safety_margin = 16;
        const int text_width = static_cast<int>(bitrate_str.length()) * (sf::TEXT_CHAR_WIDTH_LARGE + 2) + safety_margin;
        const int start_x = width - text_width - padding_x;
        const int start_y = height - sf::TEXT_CHAR_HEIGHT_LARGE - padding_y;

        if (start_x >= 0 && start_y >= 0 && start_y + sf::TEXT_CHAR_HEIGHT_LARGE <= height) {
            text_position = std::make_pair(start_x, start_y);
            draw_large_text = true;
        }
    } else if (pipeline->draw_bitrate) {
        const auto position_opt = calculate_bitrate_text_position(bitrate_str.length(), width, height);
        if (!position_opt) {
            return GST_PAD_PROBE_OK;
        }
        text_position = position_opt;
    }

    if (!text_position.has_value()) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *buffer = prepare_writable_buffer(info, pipeline);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    batch_meta = sf::ensure_nvds_batch_meta(buffer, width, height);
    if (batch_meta == nullptr) {
        if (!pipeline->stats_context.overlay_state.bitrate_warned_create_meta) {
            sf::warning(display_branch_category, "Display overlays: failed to attach NvDsBatchMeta to buffer");
            pipeline->stats_context.overlay_state.bitrate_warned_create_meta = true;
        }
        return GST_PAD_PROBE_OK;
    }

    clear_display_meta_if_needed(batch_meta, pipeline);

    if (!pipeline->stats_context.overlay_state.bitrate_probe_logged) {
        sf::info(display_branch_category, "Display bitrate probe active: %dx%d", width, height);
        pipeline->stats_context.overlay_state.bitrate_probe_logged = true;
    }

    const auto [start_x, start_y] = text_position.value();
    if (draw_large_text) {
        if (!sf::add_text_to_display_meta(batch_meta, bitrate_str.c_str(), start_x, start_y, sf::yuv_color::red,
                                          font_size_large)) {
            sf::debug(display_branch_category, "Failed to add large red bitrate '%s' to nvosd meta",
                      bitrate_str.c_str());
        }
    } else {
        if (!sf::add_text_to_display_meta(batch_meta, bitrate_str.c_str(), start_x, start_y, sf::yuv_color::white,
                                          font_size_normal)) {
            sf::debug(display_branch_category, "Failed to add bitrate '%s' to nvosd meta", bitrate_str.c_str());
        }
    }

    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn display_fps_overlay_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    auto *pipeline = static_cast<rtp_pipeline *>(user_data);
    if (!pipeline || !pipeline->draw_fps) {
        return GST_PAD_PROBE_OK;
    }

    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }

    pipeline->stats_context.fps_monitor.record_frame("display");
    const double fps = pipeline->stats_context.fps_monitor.get_current_fps("display");
    const std::string fps_str = format_fps_string(fps);
    if (fps_str.empty()) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *read_buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!read_buffer) {
        return GST_PAD_PROBE_OK;
    }

    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(read_buffer);
    if (batch_meta == nullptr) {
        if (!pipeline->stats_context.overlay_state.fps_warned_missing_meta) {
            sf::warning(display_branch_category,
                        "Display overlays: NvDsBatchMeta missing on buffer, creating minimal meta");
            pipeline->stats_context.overlay_state.fps_warned_missing_meta = true;
        }
    }
    const auto dimensions_opt = get_frame_dimensions(pad, read_buffer, batch_meta);
    if (!dimensions_opt.has_value()) {
        return GST_PAD_PROBE_OK;
    }

    const auto [width, height] = dimensions_opt.value();
    const auto position_opt = calculate_fps_text_position(fps_str.length(), width);
    if (!position_opt.has_value()) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer *buffer = prepare_writable_buffer(info, pipeline);
    if (!buffer) {
        return GST_PAD_PROBE_OK;
    }

    batch_meta = sf::ensure_nvds_batch_meta(buffer, width, height);
    if (batch_meta == nullptr) {
        if (!pipeline->stats_context.overlay_state.fps_warned_create_meta) {
            sf::warning(display_branch_category, "Display overlays: failed to attach NvDsBatchMeta to buffer");
            pipeline->stats_context.overlay_state.fps_warned_create_meta = true;
        }
        return GST_PAD_PROBE_OK;
    }

    clear_display_meta_if_needed(batch_meta, pipeline);

    if (!pipeline->stats_context.overlay_state.fps_probe_logged) {
        sf::info(display_branch_category, "Display FPS probe active: %dx%d", width, height);
        pipeline->stats_context.overlay_state.fps_probe_logged = true;
    }

    const auto [start_x, start_y] = position_opt.value();
    constexpr int font_size_normal = 12;
    if (!sf::add_text_to_display_meta(batch_meta, fps_str.c_str(), start_x, start_y, sf::yuv_color::white,
                                      font_size_normal)) {
        sf::debug(display_branch_category, "Failed to add FPS '%s' to nvosd meta", fps_str.c_str());
    }

    (void) height;
    return GST_PAD_PROBE_OK;
}
