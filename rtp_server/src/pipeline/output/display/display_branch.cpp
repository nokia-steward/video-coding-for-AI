#include "display_branch.h"
#include "../../../core/rtp_server_config.h"
#include "../../pipeline.h"
#include "deepstream_config.h"
#include "gst/gpu_elements.h"
#include "label_utils.h"
#include "logging.h"
#include "nvosd_overlay.h"
#include "overlay_probes.h"
#include "test_mode.h"

#include <cstdlib>
#include <cstring>
#include <gst/gst.h>
#include <gst/video/video.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include <gstnvdsinfer.h>
#pragma GCC diagnostic pop
#include <gstnvdsmeta.h>
#include <memory>
#include <nvds_yml_parser.h>
#include <optional>
#include <unordered_map>
#include <vector>

// Debug category for display branch operations.
GST_DEBUG_CATEGORY(display_branch_category);
#define GST_CAT_DEFAULT display_branch_category

void init_display_branch_debug_category() {
    GST_DEBUG_CATEGORY_INIT(display_branch_category, "display-branch", 0, "Display Branch");
}

namespace {
    [[nodiscard]] bool is_ah0187_semseg_battery_mode_enabled() {
        const char *flag = std::getenv("SF_TET_COMPARISON_SEMSEG_MODE");
        if (flag == nullptr) {
            return false;
        }
        return std::strcmp(flag, "1") == 0;
    }

    [[nodiscard]] bool initialize_test_mode_context(rtp_pipeline *pipeline) {
        if (!pipeline || !pipeline->server_config) {
            return true;
        }

        const auto &groundtruth_file = pipeline->server_config->groundtruth_file;
        const auto &semseg_groundtruth_file = pipeline->server_config->semseg_groundtruth_file;
        if (groundtruth_file.empty() && semseg_groundtruth_file.empty()) {
            return true;
        }

        if (!pipeline->server_config->inference_engine) {
            sf::warning(display_branch_category,
                        "Test mode groundtruth configured but inference engine is disabled for port %u",
                        pipeline->get_port());
            return true;
        }

        auto context = std::make_unique<test_mode_context>();
        context->iou_threshold = pipeline->server_config->test_mode_iou_threshold;

        bool bbox_enabled = false;
        bool semseg_enabled = false;

        if (!groundtruth_file.empty()) {
            auto groundtruth_opt = load_groundtruth_data(groundtruth_file, display_branch_category);
            if (!groundtruth_opt) {
                sf::error(display_branch_category, "Failed to load bbox groundtruth for test mode: %s",
                          groundtruth_file.c_str());
                return false;
            }
            context->groundtruth = std::move(groundtruth_opt.value());
            bbox_enabled = true;
        }

        if (!semseg_groundtruth_file.empty()) {
            if (!is_ah0187_semseg_battery_mode_enabled()) {
                sf::error(display_branch_category,
                          "Semseg test mode in rtp_server is disabled outside AH0187 battery runs."
                          " Use scripts/run_tet_comparison.sh --mode semseg.");
                return false;
            }
            auto semseg_groundtruth_opt =
                    load_semseg_groundtruth_data(semseg_groundtruth_file, display_branch_category);
            if (!semseg_groundtruth_opt) {
                sf::error(display_branch_category, "Failed to load semseg groundtruth for test mode: %s",
                          semseg_groundtruth_file.c_str());
                return false;
            }
            context->semseg_groundtruth = std::move(semseg_groundtruth_opt.value());
            semseg_enabled = true;
        }

        if (pipeline->nvinfer && (bbox_enabled || semseg_enabled)) {
            const std::string label_file_path = get_label_file_path(pipeline->nvinfer.get());
            if (!label_file_path.empty()) {
                const auto label_names = read_label_file(label_file_path);
                if (!label_names.empty()) {
                    if (bbox_enabled && !context->groundtruth.category_names.empty()) {
                        std::unordered_map<std::string, int> groundtruth_name_to_id;
                        groundtruth_name_to_id.reserve(context->groundtruth.category_names.size());
                        for (const auto &entry: context->groundtruth.category_names) {
                            groundtruth_name_to_id.emplace(normalize_label_name(entry.second), entry.first);
                        }

                        for (size_t index = 0; index < label_names.size(); ++index) {
                            const int detection_class_id = static_cast<int>(index);
                            context->detection_class_names.emplace(detection_class_id, label_names[index]);
                            const auto groundtruth_iter =
                                    groundtruth_name_to_id.find(normalize_label_name(label_names[index]));
                            if (groundtruth_iter != groundtruth_name_to_id.end()) {
                                context->detection_class_to_groundtruth_id.emplace(detection_class_id,
                                                                                   groundtruth_iter->second);
                            }
                        }

                        if (context->detection_class_to_groundtruth_id.empty()) {
                            sf::warning(display_branch_category,
                                        "Test mode bbox: no matching groundtruth categories for detection labels");
                        } else {
                            sf::info(display_branch_category,
                                     "Test mode bbox: mapped %zu detection labels to groundtruth categories",
                                     context->detection_class_to_groundtruth_id.size());
                        }
                    }

                    if (semseg_enabled && !context->semseg_groundtruth.category_names.empty()) {
                        std::unordered_map<std::string, int> semseg_name_to_id;
                        semseg_name_to_id.reserve(context->semseg_groundtruth.category_names.size());
                        for (const auto &entry: context->semseg_groundtruth.category_names) {
                            semseg_name_to_id.emplace(normalize_label_name(entry.second), entry.first);
                        }

                        for (size_t index = 0; index < label_names.size(); ++index) {
                            const int prediction_class_id = static_cast<int>(index);
                            const auto semseg_iter = semseg_name_to_id.find(normalize_label_name(label_names[index]));
                            if (semseg_iter != semseg_name_to_id.end()) {
                                context->semseg_prediction_class_to_groundtruth_id.emplace(prediction_class_id,
                                                                                           semseg_iter->second);
                            }
                        }

                        if (context->semseg_prediction_class_to_groundtruth_id.empty()) {
                            sf::warning(display_branch_category,
                                        "Test mode semseg: no label-name mapping found; using class IDs directly");
                        } else {
                            sf::info(display_branch_category,
                                     "Test mode semseg: mapped %zu prediction labels to semseg categories",
                                     context->semseg_prediction_class_to_groundtruth_id.size());
                        }
                    }
                } else {
                    sf::warning(display_branch_category, "Test mode: failed to read label file for category mapping");
                }
            }
        }
        if (bbox_enabled && !pipeline->server_config->labels.empty()) {
            context->allowed_detection_class_ids.reserve(pipeline->class_ids_to_filter.size());
            for (const auto class_id: pipeline->class_ids_to_filter) {
                context->allowed_detection_class_ids.push_back(static_cast<int>(class_id));
            }

            const auto label_names = split_labels(pipeline->server_config->labels);
            for (const auto &label_name: label_names) {
                const auto normalized_label = normalize_label_name(label_name);
                for (const auto &entry: context->groundtruth.category_names) {
                    if (normalize_label_name(entry.second) == normalized_label) {
                        context->allowed_groundtruth_category_ids.push_back(entry.first);
                        break;
                    }
                }
            }
        }
        pipeline->test_mode_ctx = std::move(context);

        sf::info(display_branch_category, "Test mode enabled for port %u (bbox=%s semseg=%s, IoU threshold: %.3f)",
                 pipeline->get_port(), bbox_enabled ? "on" : "off", semseg_enabled ? "on" : "off",
                 pipeline->server_config->test_mode_iou_threshold);
        return true;
    }

    /**
     * @brief Build RGBA caps for display output.
     * @return Caps describing RGBA NVMM buffers.
     */
    [[nodiscard]] sf::unique_gst_caps_ptr create_display_rgba_caps() {
        return sf::unique_gst_caps_ptr(gst_caps_from_string("video/x-raw(memory:NVMM),format=RGBA"));
    }

    /**
     * @brief Convert caps to a readable string for logging.
     * @param caps Caps to stringify (may be null).
     * @return String representation or "null" if unavailable.
     */
    [[nodiscard]] std::string caps_to_string(GstCaps *caps) {
        if (!caps) {
            return "null";
        }
        char *caps_string = gst_caps_to_string(caps);
        if (!caps_string) {
            return "null";
        }
        std::string result(caps_string);
        g_free(caps_string);
        return result;
    }

    /**
     * @brief Log current and template caps for a specific pad.
     * @param element Element owning the pad.
     * @param pad_name Name of the pad to inspect.
     * @param element_label Label used in log messages.
     * @param port Port number for log context.
     */
    void log_pad_caps(GstElement *element, const char *pad_name, const char *element_label, guint16 port) {
        if (!element || !pad_name || !element_label) {
            sf::warning(display_branch_category, "Cannot log pad caps for port %u due to invalid inputs", port);
            return;
        }

        GstPad *pad = gst_element_get_static_pad(element, pad_name);
        if (!pad) {
            sf::error(display_branch_category, "Missing %s pad on %s for port %u", pad_name, element_label, port);
            return;
        }

        GstCaps *current_caps = gst_pad_get_current_caps(pad);
        const std::string current_caps_string = caps_to_string(current_caps);
        if (current_caps) {
            gst_caps_unref(current_caps);
        }

        GstCaps *query_caps = gst_pad_query_caps(pad, nullptr);
        const std::string query_caps_string = caps_to_string(query_caps);
        if (query_caps) {
            gst_caps_unref(query_caps);
        }

        sf::error(display_branch_category, "%s %s pad caps for port %u - current: %s, template: %s", element_label,
                  pad_name, port, current_caps_string.c_str(), query_caps_string.c_str());

        gst_object_unref(pad);
    }

    /**
     * @brief Link two elements and emit detailed caps logs on failure.
     * @param upstream Upstream element providing the src pad.
     * @param downstream Downstream element providing the sink pad.
     * @param upstream_label Label used for upstream logging.
     * @param downstream_label Label used for downstream logging.
     * @param port Port number for log context.
     * @return true if linking succeeds, false otherwise.
     */
    [[nodiscard]] bool link_elements_with_debug(GstElement *upstream, GstElement *downstream,
                                                const char *upstream_label, const char *downstream_label,
                                                guint16 port) {
        if (!upstream || !downstream) {
            sf::error(display_branch_category, "Invalid elements while linking %s -> %s for port %u", upstream_label,
                      downstream_label, port);
            return false;
        }

        if (gst_element_link(upstream, downstream) != TRUE) {
            sf::error(display_branch_category, "Failed to link %s -> %s for port %u", upstream_label, downstream_label,
                      port);
            log_pad_caps(upstream, "src", upstream_label, port);
            log_pad_caps(downstream, "sink", downstream_label, port);
            return false;
        }

        return true;
    }

    /**
     * @brief Configures a queue element with size limits.
     * @param queue The queue element to configure.
     * @param max_bytes Maximum bytes in the queue.
     * @param max_buffers Maximum number of buffers in the queue.
     * @param max_time Maximum time in the queue (nanoseconds).
     */
    void configure_queue_element(GstElement *queue, const guint64 max_bytes, const guint max_buffers,
                                 const guint64 max_time) {
        // leaky=0 means GST_QUEUE_NO_LEAK (preserve all buffers when queue is full)
        g_object_set(G_OBJECT(queue), "max-size-bytes", static_cast<guint>(max_bytes), "max-size-buffers", max_buffers,
                     "max-size-time", max_time, "leaky", 0, nullptr);
    }


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
        }

        return buffer;
    }


    /**
     * @brief Pad probe callback to update tracker dimensions dynamically from first buffer caps.
     * @param pad The pad where the probe is attached.
     * @param info Probe info containing the buffer.
     * @param user_data Pointer to rtp_pipeline.
     * @return GST_PAD_PROBE_OK to continue processing, GST_PAD_PROBE_REMOVE to remove probe after first use.
     */
    GstPadProbeReturn update_tracker_dimensions_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        auto *pipeline = static_cast<rtp_pipeline *>(user_data);
        if (!pipeline || !pipeline->nvtracker) {
            return GST_PAD_PROBE_OK;
        }

        // Only process on first buffer to get caps
        if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
            return GST_PAD_PROBE_OK;
        }

        // Get caps from the pad
        sf::unique_gst_caps_ptr caps(gst_pad_get_current_caps(pad));
        if (!caps) {
            return GST_PAD_PROBE_OK;
        }

        GstVideoInfo video_info;
        gst_video_info_init(&video_info);
        if (!gst_video_info_from_caps(&video_info, caps.get())) {
            return GST_PAD_PROBE_OK;
        }

        const int width = GST_VIDEO_INFO_WIDTH(&video_info);
        const int height = GST_VIDEO_INFO_HEIGHT(&video_info);

        if (width > 0 && height > 0) {
            // Get current tracker dimensions
            gint current_tracker_width = 0;
            gint current_tracker_height = 0;
            g_object_get(G_OBJECT(pipeline->nvtracker.get()), "tracker-width", &current_tracker_width, "tracker-height",
                         &current_tracker_height, nullptr);

            // Update if dimensions are different
            if (current_tracker_width != width || current_tracker_height != height) {
                g_object_set(G_OBJECT(pipeline->nvtracker.get()), "tracker-width", width, "tracker-height", height,
                             nullptr);
                sf::info(display_branch_category, "Updated tracker dimensions dynamically from caps: %dx%d (was %dx%d)",
                         width, height, current_tracker_width, current_tracker_height);
            }
        }

        // Remove probe after first use
        return GST_PAD_PROBE_REMOVE;
    }

    /**
     * @brief Pad probe callback for drawing borders on DeepStream-processed frames.
     *
     * This probe is attached to the display overlay chain and is called for each
     * buffer that passes through. It extracts detection metadata from the buffer, filters
     * detections by class ID if label filtering is enabled, and draws colored borders
     * around detected objects.
     *
     * The probe:
     * - Extracts NvDsBatchMeta from the buffer
     * - Filters detections by class_id if class_ids_to_filter is not empty
     * - Groups rectangles by color (currently all blue)
     * - Draws borders using libvcai border drawing functions
     * - Supports both NV12 and I420 video formats
     *
     * @param pad The pad where the probe is attached.
     * @param info Probe info containing the buffer with DeepStream metadata.
     * @param user_data Pointer to rtp_pipeline containing server_config and class_ids_to_filter.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    // DeepStream handles bounding boxes via nvdsosd when enabled.

    /**
     * @brief Clears object labels so only boxes are rendered.
     * @param pipeline Pipeline that owns the display branch.
     * @return true if probe was attached or not needed, false on failure.
     */
    [[nodiscard]] bool add_display_label_cleanup_probe(rtp_pipeline *pipeline) {
        if (!pipeline || !pipeline->nvosd_display) {
            return false;
        }

        GstPad *pad = gst_element_get_static_pad(pipeline->nvosd_display.get(), "sink");
        if (!pad) {
            sf::warning(display_branch_category, "Failed to get nvosd sink pad for label cleanup probe");
            return false;
        }

        const gulong probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad *probe_pad, GstPadProbeInfo *info, gpointer user_data) {
                    (void) probe_pad;
                    auto *display_pipeline = static_cast<rtp_pipeline *>(user_data);
                    if (!display_pipeline) {
                        return GST_PAD_PROBE_OK;
                    }

                    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
                        return GST_PAD_PROBE_OK;
                    }

                    GstBuffer *buffer = prepare_writable_buffer(info, display_pipeline);
                    if (!buffer) {
                        return GST_PAD_PROBE_OK;
                    }

                    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
                    if (batch_meta == nullptr) {
                        return GST_PAD_PROBE_OK;
                    }

                    for (NvDsMetaList *frame_list = batch_meta->frame_meta_list; frame_list != nullptr;
                         frame_list = frame_list->next) {
                        auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(frame_list->data);
                        if (!frame_meta) {
                            continue;
                        }

                        for (NvDsMetaList *obj_list = frame_meta->obj_meta_list; obj_list != nullptr;
                             obj_list = obj_list->next) {
                            auto *obj_meta = reinterpret_cast<NvDsObjectMeta *>(obj_list->data);
                            if (!obj_meta) {
                                continue;
                            }

                            obj_meta->text_params.display_text = 0;
                        }
                    }

                    return GST_PAD_PROBE_OK;
                },
                pipeline, nullptr);
        if (probe_id == 0) {
            sf::warning(display_branch_category, "Failed to attach label cleanup probe to nvosd");
            gst_object_unref(pad);
            return false;
        }

        sf::info(display_branch_category, "Attached label cleanup probe to nvosd");
        gst_object_unref(pad);
        return true;
    }

    [[nodiscard]] bool add_display_sink_probe(rtp_pipeline *pipeline) {
        if (!pipeline || !pipeline->output_sink) {
            return false;
        }

        GstPad *sink_pad = gst_element_get_static_pad(pipeline->output_sink.get(), "sink");
        if (!sink_pad) {
            sf::warning(display_branch_category, "Failed to get display sink pad for probe");
            return false;
        }

        const gulong probe_id = gst_pad_add_probe(
                sink_pad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
                    auto *display_pipeline = static_cast<rtp_pipeline *>(user_data);
                    if (!display_pipeline) {
                        return GST_PAD_PROBE_OK;
                    }
                    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
                        return GST_PAD_PROBE_OK;
                    }

                    if (!display_pipeline->stats_context.overlay_state.display_sink_first_buffer_logged) {
                        GstCaps *caps = gst_pad_get_current_caps(pad);
                        const sf::unique_g_char_ptr caps_string(caps ? gst_caps_to_string(caps) : nullptr);
                        if (caps) {
                            gst_caps_unref(caps);
                        }
                        sf::warning(display_branch_category, "First buffer reached display sink with caps: %s",
                                    caps_string ? caps_string.get() : "null");
                        display_pipeline->stats_context.overlay_state.display_sink_first_buffer_logged = true;
                    }

                    return GST_PAD_PROBE_OK;
                },
                pipeline, nullptr);

        gst_object_unref(sink_pad);
        if (probe_id == 0) {
            sf::warning(display_branch_category, "Failed to attach display sink probe");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool add_nvosd_src_probe(rtp_pipeline *pipeline) {
        if (!pipeline || !pipeline->nvosd_display) {
            return false;
        }

        GstPad *src_pad = gst_element_get_static_pad(pipeline->nvosd_display.get(), "src");
        if (!src_pad) {
            sf::warning(display_branch_category, "Failed to get nvosd src pad for probe");
            return false;
        }

        const gulong probe_id = gst_pad_add_probe(
                src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
                    auto *display_pipeline = static_cast<rtp_pipeline *>(user_data);
                    if (!display_pipeline) {
                        return GST_PAD_PROBE_OK;
                    }
                    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
                        return GST_PAD_PROBE_OK;
                    }

                    if (!display_pipeline->stats_context.overlay_state.nvosd_src_first_buffer_logged) {
                        GstCaps *caps = gst_pad_get_current_caps(pad);
                        const sf::unique_g_char_ptr caps_string(caps ? gst_caps_to_string(caps) : nullptr);
                        if (caps) {
                            gst_caps_unref(caps);
                        }
                        sf::warning(display_branch_category, "First buffer reached nvosd src with caps: %s",
                                    caps_string ? caps_string.get() : "null");
                        display_pipeline->stats_context.overlay_state.nvosd_src_first_buffer_logged = true;
                    }

                    return GST_PAD_PROBE_OK;
                },
                pipeline, nullptr);

        gst_object_unref(src_pad);
        if (probe_id == 0) {
            sf::warning(display_branch_category, "Failed to attach nvosd src probe");
            return false;
        }

        return true;
    }

    [[nodiscard]] GstElement *get_inference_probe_element(rtp_pipeline *pipeline) {
        if (!pipeline) {
            return nullptr;
        }

        // Test mode probe location: currently after nvinfer.
        // To compare tracked output instead, switch to pipeline->nvtracker.
        return pipeline->nvinfer.get();
    }

    [[nodiscard]] std::vector<detection_box> extract_detections_from_frame_meta(const NvDsFrameMeta *frame_meta) {
        std::vector<detection_box> detections;
        if (!frame_meta) {
            return detections;
        }

        for (NvDsMetaList *obj_list = frame_meta->obj_meta_list; obj_list != nullptr; obj_list = obj_list->next) {
            auto *obj_meta = reinterpret_cast<NvDsObjectMeta *>(obj_list->data);
            if (!obj_meta) {
                continue;
            }

            const auto &rect = obj_meta->rect_params;
            detection_box detection{};
            detection.class_id = obj_meta->class_id;
            detection.x_coord = static_cast<float>(rect.left);
            detection.y_coord = static_cast<float>(rect.top);
            detection.width = static_cast<float>(rect.width);
            detection.height = static_cast<float>(rect.height);
            detection.confidence = obj_meta->confidence;
            detections.push_back(detection);
        }

        return detections;
    }

    [[nodiscard]] std::optional<semseg_prediction_map>
    extract_semseg_prediction_from_frame_meta(const NvDsFrameMeta *frame_meta) {
        if (!frame_meta) {
            return std::nullopt;
        }

        for (NvDsMetaList *user_meta_list = frame_meta->frame_user_meta_list; user_meta_list != nullptr;
             user_meta_list = user_meta_list->next) {
            auto *user_meta = reinterpret_cast<NvDsUserMeta *>(user_meta_list->data);
            if (!user_meta || user_meta->base_meta.meta_type != NVDSINFER_SEGMENTATION_META) {
                continue;
            }

            auto *seg_meta = reinterpret_cast<NvDsInferSegmentationMeta *>(user_meta->user_meta_data);
            if (!seg_meta || seg_meta->width <= 0 || seg_meta->height <= 0 || !seg_meta->class_map) {
                continue;
            }

            semseg_prediction_map prediction{};
            prediction.width = static_cast<int>(seg_meta->width);
            prediction.height = static_cast<int>(seg_meta->height);
            const size_t pixel_count = static_cast<size_t>(seg_meta->width * seg_meta->height);
            prediction.class_map.reserve(pixel_count);
            for (size_t index = 0; index < pixel_count; ++index) {
                prediction.class_map.push_back(seg_meta->class_map[index]);
            }
            return prediction;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool add_inference_test_mode_probe(rtp_pipeline *pipeline) {
        if (!pipeline || !pipeline->test_mode_ctx) {
            return false;
        }

        GstElement *probe_element = get_inference_probe_element(pipeline);
        if (!probe_element) {
            return false;
        }

        GstPad *src_pad = gst_element_get_static_pad(probe_element, "src");
        if (!src_pad) {
            sf::warning(display_branch_category, "Failed to get inference src pad for test mode probe");
            return false;
        }

        const gulong probe_id = gst_pad_add_probe(
                src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                [](GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
                    (void) pad;
                    auto *pipeline_ctx = static_cast<rtp_pipeline *>(user_data);
                    if (!pipeline_ctx || !pipeline_ctx->test_mode_ctx) {
                        return GST_PAD_PROBE_OK;
                    }

                    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
                        return GST_PAD_PROBE_OK;
                    }

                    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                    if (!buffer) {
                        return GST_PAD_PROBE_OK;
                    }

                    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
                    if (!batch_meta) {
                        return GST_PAD_PROBE_OK;
                    }

                    // Use the original decoded frame number stored in GST_BUFFER_OFFSET
                    // by the decoded frame processor. DeepStream's frame_meta->frame_num is
                    // a sequential counter from nvstreammux that does not correspond to the
                    // encoder's frame index. The buffer offset is set to the 1-based
                    // decoded_frame_count, so subtract 1 for the 0-based groundtruth index.
                    const guint64 buffer_offset = GST_BUFFER_OFFSET(buffer);
                    const bool has_original_frame_number = (buffer_offset != GST_BUFFER_OFFSET_NONE);

                    for (NvDsMetaList *frame_list = batch_meta->frame_meta_list; frame_list != nullptr;
                         frame_list = frame_list->next) {
                        auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(frame_list->data);
                        if (!frame_meta) {
                            continue;
                        }

                        ++pipeline_ctx->stats_context.inference_frames_observed;
                        pipeline_ctx->runtime_context.notify_state_change();
                        const int frame_index =
                                has_original_frame_number ? static_cast<int>(buffer_offset) - 1 : frame_meta->frame_num;
                        if (!pipeline_ctx->test_mode_ctx->groundtruth.frames.empty()) {
                            const auto detections = extract_detections_from_frame_meta(frame_meta);
                            evaluate_test_mode_frame(*pipeline_ctx->test_mode_ctx, frame_index, detections,
                                                     display_branch_category);
                        }

                        if (!pipeline_ctx->test_mode_ctx->semseg_groundtruth.points_by_frame.empty()) {
                            const auto prediction_opt = extract_semseg_prediction_from_frame_meta(frame_meta);
                            if (prediction_opt.has_value()) {
                                int source_frame_width = prediction_opt->width;
                                if (frame_meta->source_frame_width > 0) {
                                    source_frame_width = static_cast<int>(frame_meta->source_frame_width);
                                }
                                int source_frame_height = prediction_opt->height;
                                if (frame_meta->source_frame_height > 0) {
                                    source_frame_height = static_cast<int>(frame_meta->source_frame_height);
                                }
                                evaluate_semseg_test_mode_frame(*pipeline_ctx->test_mode_ctx, frame_index,
                                                                prediction_opt.value(), source_frame_width,
                                                                source_frame_height, display_branch_category);
                            }
                        }
                    }

                    return GST_PAD_PROBE_OK;
                },
                pipeline, nullptr);

        gst_object_unref(src_pad);
        if (probe_id == 0) {
            sf::warning(display_branch_category, "Failed to attach inference test mode probe");
            return false;
        }

        sf::info(display_branch_category, "Attached inference test mode probe");
        return true;
    }
} // namespace

/**
 * @brief Creates the display branch function table for the output pipeline.
 *
 * The display branch handles video output to an autovideosink. When inference is enabled,
 * it integrates DeepStream elements (nvinfer, nvtracker) for object detection and tracking,
 * and optionally draws borders around detected objects.
 *
 * @return output_branch_functions structure containing all branch function pointers.
 */
output_branch_functions create_display_branch_functions() {
    return {// Display branch is always enabled
            .is_enabled = [](const rtp_pipeline *pipeline) -> bool {
                (void) pipeline;
                return true;
            },

            .create_elements = [](rtp_pipeline *pipeline) -> bool {
                if (!pipeline) {
                    return false;
                }

                // Create shared output elements (gpu_video_convert and tee)
                // These are created by display branch but shared with all output branches
                // gpu_video_convert: keeps frames in CUDA memory
                // tee: splits the video stream to multiple output branches (display, file, RTP output)
                try {
                    // RAII_EXEMPT: GstBin takes ownership of this raw GstElement pointer.
                    pipeline->gpu_video_convert = sf::unique_gst_element_no_delete_ptr(
                            create_gpu_video_convert("output-gpu-convert").release());
                } catch (const std::exception &error) {
                    sf::error(display_branch_category, "Failed to create GPU converter for port %u: %s",
                              pipeline->get_port(), error.what());
                    return false;
                }

                pipeline->tee = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("tee", "tee"));
                if (!pipeline->tee) {
                    sf::error(display_branch_category, "Failed to create tee for port %u", pipeline->get_port());
                    return false;
                }

                // Create display branch elements.
                // queue_display: large buffer to prevent frame drops during display processing
                // output_sink: on-screen sink or headless fakesink depending on configuration
                pipeline->queue_display =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
                const bool display_sink_enabled =
                        pipeline->server_config == nullptr || pipeline->server_config->enable_display_sink;
                pipeline->output_sink = sf::unique_gst_element_no_delete_ptr(
                        gst_element_factory_make(display_sink_enabled ? "nveglglessink" : "fakesink", "sink"));
                if (!pipeline->output_sink) {
                    sf::error(display_branch_category, "Failed to create %s sink for port %u",
                              display_sink_enabled ? "nveglglessink" : "fakesink", pipeline->get_port());
                    return false;
                }
                if (!display_sink_enabled) {
                    sf::info(display_branch_category, "Display sink disabled for port %u; using fakesink headless mode",
                             pipeline->get_port());
                }
                // RAII_EXEMPT: GstBin takes ownership of this raw GstElement pointer.
                pipeline->nvosd_display =
                        sf::unique_gst_element_no_delete_ptr(create_nvosd_element("nvosd-display").release());
                pipeline->capsfilter_display =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("capsfilter", "display-caps"));
                const bool save_frames_enabled =
                        pipeline->server_config != nullptr && pipeline->server_config->save_frames;
                if (save_frames_enabled) {
                    pipeline->post_osd_tee =
                            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("tee", "post-osd-tee"));
                }
                try {
                    // RAII_EXEMPT: GstBin takes ownership of this raw GstElement pointer.
                    pipeline->display_convert =
                            sf::unique_gst_element_no_delete_ptr(create_gpu_video_convert("display-convert").release());
                } catch (const std::exception &error) {
                    sf::error(display_branch_category, "Failed to create display converter for port %u: %s",
                              pipeline->get_port(), error.what());
                    return false;
                }

                if (!pipeline->queue_display || !pipeline->output_sink || !pipeline->nvosd_display ||
                    !pipeline->display_convert || !pipeline->capsfilter_display) {
                    sf::error(display_branch_category, "Failed to create display branch elements for port %u",
                              pipeline->get_port());
                    return false;
                }
                if (save_frames_enabled && !pipeline->post_osd_tee) {
                    sf::error(display_branch_category, "Failed to create post-OSD tee for debug frame capture on %u",
                              pipeline->get_port());
                    return false;
                }

                // Create DeepStream elements if inference is enabled
                // DeepStream pipeline structure:
                //   display_convert: converts to RGBA CUDA memory
                //   capsfilter_display: ensures CUDA RGBA caps
                //   nvstreammux: batches frames from input sources
                //   nvinfer: runs inference (object detection)
                //   nvtracker: tracks objects across frames (optional)
                if (pipeline->server_config && pipeline->server_config->inference_engine) {
                    pipeline->nvstreammux = sf::unique_gst_element_no_delete_ptr(
                            gst_element_factory_make("nvstreammux", "stream-muxer"));
                    pipeline->nvinfer = sf::unique_gst_element_no_delete_ptr(
                            gst_element_factory_make("nvinfer", "primary-nvinference-engine"));

                    if (!pipeline->nvstreammux || !pipeline->nvinfer) {
                        sf::error(display_branch_category,
                                  "Failed to create DeepStream elements for inference on port %u",
                                  pipeline->get_port());
                        return false;
                    }

                    // Create tracker if enabled (tracks objects across frames using Kalman filtering)
                    if (pipeline->server_config->tracker_enabled) {
                        pipeline->nvtracker = sf::unique_gst_element_no_delete_ptr(
                                gst_element_factory_make("nvtracker", "nvtracker"));
                        if (!pipeline->nvtracker) {
                            sf::error(display_branch_category, "Failed to create nvtracker element for port %u",
                                      pipeline->get_port());
                            return false;
                        }
                    }

                    sf::info(display_branch_category, "Created DeepStream elements for inference on port %u",
                             pipeline->get_port());
                }

                return true;
            },

            .configure_elements = [](rtp_pipeline *pipeline) -> bool {
                if (!pipeline) {
                    sf::error(display_branch_category, "Pipeline is null in configure_elements");
                    return false;
                }

                // Configure display queue with no leak to preserve all frames.
                // In test mode, use larger limits so all frames can be buffered while
                // inference processes them one by one (inference can take >1s per frame).
                {
                    const bool test_mode =
                            pipeline->server_config && (!pipeline->server_config->groundtruth_file.empty() ||
                                                        !pipeline->server_config->semseg_groundtruth_file.empty());
                    const guint64 max_bytes = test_mode ? static_cast<guint64>(500) * 1024 * 1024
                                                        : static_cast<guint64>(50) * 1024 * 1024;
                    const guint max_buffers = test_mode ? 500u : 90u;
                    const guint64 max_time = test_mode ? 0 : 3000 * GST_MSECOND; // 0 = unlimited time
                    configure_queue_element(pipeline->queue_display.get(), max_bytes, max_buffers, max_time);
                    if (test_mode) {
                        sf::info(display_branch_category,
                                 "Test mode: display queue enlarged for port %u — "
                                 "max-size-bytes=%lu, max-size-buffers=%u, max-size-time=unlimited",
                                 pipeline->get_port(), max_bytes, max_buffers);
                    }
                }

                // Configure display sink: sync=false allows it to display frames as fast as they arrive
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(pipeline->output_sink.get()), "sync") != nullptr) {
                    g_object_set(G_OBJECT(pipeline->output_sink.get()), "sync", FALSE, nullptr);
                }
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(pipeline->output_sink.get()), "qos") != nullptr) {
                    g_object_set(G_OBJECT(pipeline->output_sink.get()), "qos", FALSE, nullptr);
                }
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(pipeline->output_sink.get()),
                                                 "enable-last-sample") != nullptr) {
                    g_object_set(G_OBJECT(pipeline->output_sink.get()), "enable-last-sample", FALSE, nullptr);
                }
                if (g_object_class_find_property(G_OBJECT_GET_CLASS(pipeline->output_sink.get()), "async") != nullptr) {
                    g_object_set(G_OBJECT(pipeline->output_sink.get()), "async", FALSE, nullptr);
                }
                sf::warning(display_branch_category, "Display sink element: %s for port %u",
                            GST_ELEMENT_NAME(pipeline->output_sink.get()), pipeline->get_port());

                if (pipeline->capsfilter_display) {
                    if (pipeline->display_convert) {
                        auto *display_convert_class = G_OBJECT_GET_CLASS(pipeline->display_convert.get());
                        if (g_object_class_find_property(display_convert_class, "nvbuf-memory-type") != nullptr) {
                            g_object_set(pipeline->display_convert.get(), "nvbuf-memory-type", 3, nullptr);
                            sf::warning(display_branch_category,
                                        "Display converter using nvbuf-memory-type=3 for NVMM RGBA");
                        }
                    }

                    const sf::unique_gst_caps_ptr caps(create_display_rgba_caps());
                    if (caps) {
                        g_object_set(G_OBJECT(pipeline->capsfilter_display.get()), "caps", caps.get(), nullptr);
                        const sf::unique_g_char_ptr caps_string(gst_caps_to_string(caps.get()));
                        sf::warning(display_branch_category, "Display capsfilter caps set to: %s",
                                    caps_string ? caps_string.get() : "null");
                    } else {
                        sf::warning(display_branch_category, "Failed to create RGBA caps for display branch");
                    }
                }

                // Configure DeepStream elements if inference is enabled
                if (!pipeline->server_config) {
                    sf::error(display_branch_category, "server_config is null for port %u", pipeline->get_port());
                    return false;
                }

                if (pipeline->nvosd_display) {
                    auto *nvosd_class = G_OBJECT_GET_CLASS(pipeline->nvosd_display.get());
                    if (g_object_class_find_property(nvosd_class, "display-bbox") != nullptr) {
                        g_object_set(pipeline->nvosd_display.get(), "display-bbox",
                                     pipeline->server_config->draw_borders ? TRUE : FALSE, nullptr);
                    }
                }

                if (!pipeline->server_config->inference_engine) {
                    sf::info(display_branch_category,
                             "Inference engine disabled, skipping DeepStream configuration for port %u",
                             pipeline->get_port());
                    return true;
                }

                sf::info(display_branch_category, "Configuring DeepStream elements for inference on port %u",
                         pipeline->get_port());

                // Configure nvstreammux (stream multiplexer for batching frames)
                // nvstreammux batches frames from multiple sources (in our case, just one source)
                // and requires width/height to be set before state transitions
                const std::string config_file = pipeline->server_config->deepstream_config_file;
                sf::info(display_branch_category, "DeepStream config file path: '%s' (empty: %s)", config_file.c_str(),
                         config_file.empty() ? "yes" : "no");
                constexpr int default_width = 1920;
                constexpr int default_height = 1080;

                const bool live_source_enabled =
                        pipeline->server_config->communication_transport_type == transport_type::rtp ||
                        pipeline->server_config->communication_transport_type == transport_type::webrtc;
                if (!config_file.empty()) {
                    // Parse streammux configuration from YAML file
                    const auto parse_result = nvds_parse_streammux(
                            pipeline->nvstreammux.get(), const_cast<char *>(config_file.c_str()), "streammux");
                    if (NVDS_YAML_PARSER_SUCCESS != parse_result) {
                        sf::error(display_branch_category, "Failed to configure nvstreammux with config file: %s",
                                  config_file.c_str());
                        return false;
                    }
                    if (pipeline->server_config && pipeline->server_config->video_width > 0 &&
                        pipeline->server_config->video_height > 0) {
                        g_object_set(G_OBJECT(pipeline->nvstreammux.get()), "width",
                                     pipeline->server_config->video_width, "height",
                                     pipeline->server_config->video_height, nullptr);
                        sf::info(display_branch_category, "Overriding nvstreammux resolution from config to %dx%d",
                                 pipeline->server_config->video_width, pipeline->server_config->video_height);
                    }
                    // Ensure width/height are set even if config file doesn't specify them
                    // (nvstreammux requires these properties to be set for state transitions)
                    gint current_width = 0;
                    gint current_height = 0;
                    g_object_get(G_OBJECT(pipeline->nvstreammux.get()), "width", &current_width, "height",
                                 &current_height, nullptr);
                    if (current_width == 0 || current_height == 0) {
                        g_object_set(G_OBJECT(pipeline->nvstreammux.get()), "width", default_width, "height",
                                     default_height, nullptr);
                        sf::info(display_branch_category,
                                 "Config file did not specify width/height, using defaults: %dx%d", default_width,
                                 default_height);
                    }
                    if (g_object_class_find_property(G_OBJECT_GET_CLASS(pipeline->nvstreammux.get()), "live-source") !=
                        nullptr) {
                        g_object_set(G_OBJECT(pipeline->nvstreammux.get()), "live-source", live_source_enabled ? 1 : 0,
                                     nullptr);
                    }
                } else {
                    // Basic configuration without config file
                    // Set default dimensions (will be updated dynamically from caps if needed)
                    // batch-size=1: single stream, batched-push-timeout=40000: 40ms timeout for batching
                    g_object_set(G_OBJECT(pipeline->nvstreammux.get()), "batch-size", 1, "batched-push-timeout", 40000,
                                 "width", default_width, "height", default_height, "live-source",
                                 live_source_enabled ? 1 : 0, nullptr);
                    sf::info(display_branch_category,
                             "Configured nvstreammux with defaults: batch-size=1, timeout=40000, resolution=%dx%d",
                             default_width, default_height);
                }

                // When test mode is active, override nvstreammux to prevent frame drops.
                // In normal streaming mode, live-source=1 and a short batched-push-timeout
                // are appropriate to keep latency low. But in test mode, every frame must
                // reach inference for accurate evaluation. Disabling live-source stops
                // nvstreammux from discarding "late" buffers, and a large timeout gives
                // nvinfer enough time to process each frame before the next one is batched.
                const bool test_mode_active = !pipeline->server_config->groundtruth_file.empty() ||
                                              !pipeline->server_config->semseg_groundtruth_file.empty();
                if (test_mode_active) {
                    // 4s timeout: generous enough for heavy models (e.g. YOLOv11x)
                    constexpr guint batched_push_timeout_test_mode = 4000000;
                    if (g_object_class_find_property(G_OBJECT_GET_CLASS(pipeline->nvstreammux.get()), "live-source") !=
                        nullptr) {
                        g_object_set(G_OBJECT(pipeline->nvstreammux.get()), "live-source", 0, nullptr);
                    }
                    g_object_set(G_OBJECT(pipeline->nvstreammux.get()), "batched-push-timeout",
                                 batched_push_timeout_test_mode, nullptr);
                    sf::info(display_branch_category,
                             "Test mode: overriding nvstreammux for port %u — live-source=0, "
                             "batched-push-timeout=%u (no frame drops)",
                             pipeline->get_port(), batched_push_timeout_test_mode);
                }

                // Configure nvinfer (config file is required)
                if (config_file.empty()) {
                    sf::error(display_branch_category,
                              "DeepStream config file is required when inference engine is enabled. "
                              "Please set deepstream_config_file in config file or use "
                              "--inference-deepstream-config-file "
                              "option.");
                    return false;
                }

                if (!configure_nvinfer(pipeline->nvinfer.get(), config_file)) {
                    sf::error(display_branch_category, "Failed to configure nvinfer element");
                    return false;
                }

                // Get class IDs for label filtering
                if (pipeline->nvinfer && pipeline->server_config && !pipeline->server_config->labels.empty()) {
                    const std::vector<std::string> labels = split_labels(pipeline->server_config->labels);
                    if (!labels.empty()) {
                        pipeline->class_ids_to_filter = get_class_ids(pipeline->nvinfer.get(), labels);
                        if (pipeline->class_ids_to_filter.empty()) {
                            sf::warning(display_branch_category,
                                        "No matching class IDs found for labels '%s'. Showing all detections.",
                                        pipeline->server_config->labels.c_str());
                        } else {
                            sf::info(display_branch_category, "Label filtering enabled: %zu class IDs for labels '%s'",
                                     pipeline->class_ids_to_filter.size(), pipeline->server_config->labels.c_str());
                        }
                    }
                } else {
                    // No labels specified - show all detections
                    pipeline->class_ids_to_filter.clear();
                    sf::info(display_branch_category, "No label filter specified - showing all detections");
                }

                // Configure nvtracker if enabled
                // Tracker dimensions are determined dynamically from nvstreammux or decoded_output_capsfilter caps
                // A pad probe is added later to update dimensions from the first buffer if needed
                if (pipeline->server_config->tracker_enabled && pipeline->nvtracker) {
                    // Try to get video dimensions dynamically from nvstreammux or decoded_output_capsfilter caps
                    int width = 1920; // Default fallback
                    int height = 1080; // Default fallback

                    // First, try to get dimensions from nvstreammux (which we just configured)
                    // This is the most reliable source since nvstreammux is configured with actual video dimensions
                    if (pipeline->nvstreammux) {
                        guint streammux_width = 0;
                        guint streammux_height = 0;
                        g_object_get(G_OBJECT(pipeline->nvstreammux.get()), "width", &streammux_width, "height",
                                     &streammux_height, nullptr);
                        if (streammux_width > 0 && streammux_height > 0) {
                            width = static_cast<int>(streammux_width);
                            height = static_cast<int>(streammux_height);
                            sf::info(display_branch_category, "Got tracker dimensions from nvstreammux: %dx%d", width,
                                     height);
                        }
                    }

                    // If nvstreammux doesn't have dimensions yet, try decoded_output_capsfilter template/query caps
                    // This is a fallback for cases where caps are set before nvstreammux configuration
                    if ((width == 1920 && height == 1080) && pipeline->decoded_output_capsfilter) {
                        GstPad *src_pad = gst_element_get_static_pad(pipeline->decoded_output_capsfilter.get(), "src");
                        if (src_pad) {
                            // Try template caps first (set during element creation)
                            GstCaps *template_caps = gst_pad_get_pad_template_caps(src_pad);
                            if (template_caps && !gst_caps_is_empty(template_caps)) {
                                GstVideoInfo video_info;
                                gst_video_info_init(&video_info);
                                if (gst_video_info_from_caps(&video_info, template_caps)) {
                                    width = GST_VIDEO_INFO_WIDTH(&video_info);
                                    height = GST_VIDEO_INFO_HEIGHT(&video_info);
                                    sf::info(display_branch_category,
                                             "Got tracker dimensions from decoded_output_capsfilter template caps: "
                                             "%dx%d",
                                             width, height);
                                }
                            }
                            if (template_caps) {
                                gst_caps_unref(template_caps);
                            }

                            // If template caps didn't work, try query caps
                            if (width == 1920 && height == 1080) {
                                GstCaps *caps = gst_pad_query_caps(src_pad, nullptr);
                                if (caps && !gst_caps_is_empty(caps)) {
                                    GstVideoInfo video_info;
                                    gst_video_info_init(&video_info);
                                    if (gst_video_info_from_caps(&video_info, caps)) {
                                        width = GST_VIDEO_INFO_WIDTH(&video_info);
                                        height = GST_VIDEO_INFO_HEIGHT(&video_info);
                                        sf::info(display_branch_category,
                                                 "Got tracker dimensions from decoded_output_capsfilter query caps: "
                                                 "%dx%d",
                                                 width, height);
                                    }
                                }
                                if (caps) {
                                    gst_caps_unref(caps);
                                }
                            }

                            gst_object_unref(src_pad);
                        }
                    }

                    sf::info(display_branch_category, "Configuring tracker with resolution: %dx%d", width, height);
                    if (!configure_nvtracker(pipeline->nvtracker.get(), pipeline->server_config->tracker_config_file,
                                             pipeline->server_config->tracker_ll_lib_file, width, height)) {
                        return false;
                    }
                }

                if (!initialize_test_mode_context(pipeline)) {
                    return false;
                }

                return true;
            },

            .link_branch = [](rtp_pipeline *pipeline) -> bool {
                if (!pipeline) {
                    return false;
                }

                const auto link_osd_to_sink = [pipeline]() -> bool {
                    if (!pipeline->nvosd_display || !pipeline->output_sink) {
                        return false;
                    }
                    if (pipeline->post_osd_tee) {
                        if (!link_elements_with_debug(pipeline->nvosd_display.get(), pipeline->post_osd_tee.get(),
                                                      "nvosd_display", "post_osd_tee", pipeline->get_port())) {
                            return false;
                        }
                        return link_elements_with_debug(pipeline->post_osd_tee.get(), pipeline->output_sink.get(),
                                                        "post_osd_tee", "output_sink", pipeline->get_port());
                    }
                    return link_elements_with_debug(pipeline->nvosd_display.get(), pipeline->output_sink.get(),
                                                    "nvosd_display", "output_sink", pipeline->get_port());
                };

                // Link display branch based on whether inference is enabled
                if (pipeline->server_config && pipeline->server_config->inference_engine) {
                    // DeepStream pipeline: queue_display -> display_convert -> capsfilter_display ->
                    // nvstreammux -> nvinfer -> (nvtracker) -> nvosd -> (post_osd_tee) -> output_sink

                    if (!link_elements_with_debug(pipeline->queue_display.get(), pipeline->display_convert.get(),
                                                  "queue_display", "display_convert", pipeline->get_port())) {
                        return false;
                    }

                    if (!link_elements_with_debug(pipeline->display_convert.get(), pipeline->capsfilter_display.get(),
                                                  "display_convert", "capsfilter_display", pipeline->get_port())) {
                        return false;
                    }

                    // Link capsfilter_display -> nvstreammux (request pad)
                    constexpr gchar pad_name_sink[16] = "sink_0";
                    const sf::unique_gst_pad_ptr sinkpad(
                            gst_element_request_pad_simple(pipeline->nvstreammux.get(), pad_name_sink));
                    if (!sinkpad) {
                        sf::error(display_branch_category, "Failed to request sink pad from nvstreammux");
                        return false;
                    }

                    const sf::unique_gst_pad_ptr srcpad(
                            gst_element_get_static_pad(pipeline->capsfilter_display.get(), "src"));
                    if (!srcpad) {
                        sf::error(display_branch_category, "Failed to get src pad from capsfilter_display");
                        return false;
                    }

                    if (gst_pad_link(srcpad.get(), sinkpad.get()) != GST_PAD_LINK_OK) {
                        sf::error(display_branch_category, "Failed to link capsfilter_display to nvstreammux");
                        return false;
                    }

                    // Add pad probe to update tracker dimensions dynamically from first buffer caps
                    // This ensures tracker uses actual video resolution even if caps are negotiated after configuration
                    if (pipeline->server_config->tracker_enabled && pipeline->nvtracker) {
                        const sf::unique_gst_pad_ptr tracker_update_pad(
                                gst_element_get_static_pad(pipeline->nvstreammux.get(), pad_name_sink));
                        if (tracker_update_pad) {
                            gst_pad_add_probe(tracker_update_pad.get(), GST_PAD_PROBE_TYPE_BUFFER,
                                              reinterpret_cast<GstPadProbeCallback>(update_tracker_dimensions_probe),
                                              pipeline, nullptr);
                            sf::info(display_branch_category,
                                     "Added pad probe to update tracker dimensions dynamically from first buffer");
                        }
                    }

                    // Link nvstreammux -> nvinfer -> (nvtracker) -> nvosd
                    if (pipeline->server_config->tracker_enabled && pipeline->nvtracker) {
                        if (!gst_element_link_many(pipeline->nvstreammux.get(), pipeline->nvinfer.get(),
                                                   pipeline->nvtracker.get(), pipeline->nvosd_display.get(), nullptr)) {
                            sf::error(display_branch_category,
                                      "Failed to link DeepStream pipeline with tracker to OSD");
                            return false;
                        }
                    } else {
                        if (!gst_element_link_many(pipeline->nvstreammux.get(), pipeline->nvinfer.get(),
                                                   pipeline->nvosd_display.get(), nullptr)) {
                            sf::error(display_branch_category,
                                      "Failed to link DeepStream pipeline without tracker to OSD");
                            return false;
                        }
                    }
                    if (!link_osd_to_sink()) {
                        sf::error(display_branch_category, "Failed to link OSD output to display sink");
                        return false;
                    }

                    sf::info(display_branch_category, "Successfully linked DeepStream display branch for port %u",
                             pipeline->get_port());
                } else {
                    // Simple pipeline: queue_display -> display_convert -> capsfilter_display -> nvosd
                    if (!gst_element_link_many(pipeline->queue_display.get(), pipeline->display_convert.get(),
                                               pipeline->capsfilter_display.get(), pipeline->nvosd_display.get(),
                                               nullptr)) {
                        sf::error(display_branch_category, "Failed to link display branch elements");
                        return false;
                    }
                    if (!link_osd_to_sink()) {
                        sf::error(display_branch_category, "Failed to link OSD output to display sink");
                        return false;
                    }
                }

                if (!add_display_overlay_probes(pipeline)) {
                    sf::warning(display_branch_category, "Failed to attach display overlay probes for port %u",
                                pipeline->get_port());
                }
                if (!add_display_label_cleanup_probe(pipeline)) {
                    sf::warning(display_branch_category, "Failed to attach label cleanup probe for port %u",
                                pipeline->get_port());
                }
                if (!add_display_sink_probe(pipeline)) {
                    sf::warning(display_branch_category, "Failed to attach display sink probe for port %u",
                                pipeline->get_port());
                }
                if (!add_nvosd_src_probe(pipeline)) {
                    sf::warning(display_branch_category, "Failed to attach nvosd src probe for port %u",
                                pipeline->get_port());
                }
                if (pipeline->test_mode_ctx && !add_inference_test_mode_probe(pipeline)) {
                    sf::warning(display_branch_category, "Failed to attach inference test mode probe for port %u",
                                pipeline->get_port());
                }

                return true;
            },

            .link_to_tee = [](rtp_pipeline *pipeline, GstElement *tee_element) -> bool {
                if (!pipeline || !tee_element) {
                    return false;
                }

                // Link tee src pad to queue_display sink pad
                const sf::unique_gst_pad_ptr teesrc_pad(gst_element_request_pad_simple(tee_element, "src_%u"));
                const sf::unique_gst_pad_ptr sink_pad(
                        gst_element_get_static_pad(pipeline->queue_display.get(), "sink"));

                if (!teesrc_pad || !sink_pad) {
                    sf::error(display_branch_category, "Failed to get pads for display branch tee linking for port %u",
                              pipeline->get_port());
                    return false;
                }

                const GstPadLinkReturn link_ret = gst_pad_link(teesrc_pad.get(), sink_pad.get());
                if (link_ret != GST_PAD_LINK_OK) {
                    sf::error(display_branch_category, "Failed to link tee to display branch for port %u",
                              pipeline->get_port());
                    return false;
                }

                sf::info(display_branch_category, "Successfully linked display branch for port %u",
                         pipeline->get_port());
                return true;
            },

            .add_to_pipeline = [](rtp_pipeline *pipeline, GstElement *output_pipeline) -> bool {
                if (!pipeline || !output_pipeline) {
                    return false;
                }

                // Add shared output elements (gpu_video_convert and tee) and display branch elements to output pipeline
                // Shared elements are added first, then display branch-specific elements
                // DeepStream elements are only added if inference is enabled
                if (pipeline->server_config && pipeline->server_config->inference_engine) {
                    // Add DeepStream elements
                    gst_bin_add_many(GST_BIN(output_pipeline), pipeline->gpu_video_convert.get(), pipeline->tee.get(),
                                     pipeline->queue_display.get(), pipeline->display_convert.get(),
                                     pipeline->capsfilter_display.get(), pipeline->nvstreammux.get(),
                                     pipeline->nvinfer.get(), pipeline->nvosd_display.get(),
                                     pipeline->output_sink.get(), nullptr);
                    if (pipeline->post_osd_tee) {
                        gst_bin_add(GST_BIN(output_pipeline), pipeline->post_osd_tee.get());
                    }
                    if (pipeline->nvtracker) {
                        gst_bin_add(GST_BIN(output_pipeline), pipeline->nvtracker.get());
                    }
                } else {
                    // Add simple elements
                    gst_bin_add_many(GST_BIN(output_pipeline), pipeline->gpu_video_convert.get(), pipeline->tee.get(),
                                     pipeline->queue_display.get(), pipeline->display_convert.get(),
                                     pipeline->capsfilter_display.get(), pipeline->nvosd_display.get(),
                                     pipeline->output_sink.get(), nullptr);
                    if (pipeline->post_osd_tee) {
                        gst_bin_add(GST_BIN(output_pipeline), pipeline->post_osd_tee.get());
                    }
                }

                return true;
            },

            .cleanup = [](rtp_pipeline *pipeline) -> void {
                (void) pipeline;
                // No special cleanup needed - elements are managed by pipeline
            },

            .get_branch_name = []() -> const char * { return "display"; }};
}

output_branch_context create_display_branch_context() { return {.functions = create_display_branch_functions()}; }
