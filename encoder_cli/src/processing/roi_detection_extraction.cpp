// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file roi_detection_extraction.cpp
 * @brief Implements detection extraction from DeepStream metadata.
 */

#include "roi_detection_extraction.h"
#include <filesystem>
#include <fstream>
#include <gstnvdsmeta.h>
#include <mutex>
#include <ranges>
#include <sstream>
#include "../../libvcai/include/logging.h"
#include "../bridge/app/pipeline_app_adapter.h"
#include "../global_data.h"

/// GStreamer debug category for ROI detection processing.
GST_DEBUG_CATEGORY_STATIC(roi_detection_processing);
/// External declaration for the ROI detection processing debug category.
GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    struct tracker_object_dump {
        uint64_t object_id = 0;
        int class_id = -1;
        float confidence = 0.0F;
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
        bool accepted_for_detection = false;
        const char *filter_reason = "none";
    };

    std::mutex global_tracker_dump_mutex;
    std::ofstream global_tracker_dump_stream;
    bool global_tracker_dump_initialized = false;
    bool global_tracker_dump_failed = false;

    [[nodiscard]] bool ensure_tracker_dump_stream(const global_data &data) {
        if (global_tracker_dump_initialized) {
            return global_tracker_dump_stream.is_open();
        }
        if (global_tracker_dump_failed) {
            return false;
        }

        const std::filesystem::path dump_path =
                resolve_frame_output_root_directory(data.params) / "tracker_dump" / "nvtracker_output.jsonl";
        std::error_code create_error;
        std::filesystem::create_directories(dump_path.parent_path(), create_error);
        if (create_error) {
            sf::warning(roi_detection_processing, "Failed to create tracker dump directory '%s': %s",
                        dump_path.parent_path().string().c_str(), create_error.message().c_str());
            global_tracker_dump_failed = true;
            return false;
        }

        global_tracker_dump_stream.open(dump_path, std::ios::out | std::ios::trunc);
        if (!global_tracker_dump_stream.is_open()) {
            sf::warning(roi_detection_processing, "Failed to open tracker dump file at '%s'", dump_path.c_str());
            global_tracker_dump_failed = true;
            return false;
        }

        global_tracker_dump_initialized = true;
        sf::info(roi_detection_processing, "Temporary nvtracker dump enabled: %s", dump_path.c_str());
        return true;
    }

    void maybe_dump_nvtracker_frame(const global_data &data, const NvDsFrameMeta &frame_meta,
                                    const std::vector<tracker_object_dump> &objects) {
        if (!data.params.packer_logging) {
            return;
        }

        std::lock_guard lock(global_tracker_dump_mutex);
        if (!ensure_tracker_dump_stream(data)) {
            return;
        }

        std::ostringstream line;
        line << "{\"frame_number\":" << frame_meta.frame_num << ",\"source_id\":" << frame_meta.source_id
             << ",\"num_obj_meta\":" << frame_meta.num_obj_meta << ",\"objects\":[";
        for (size_t index = 0; index < objects.size(); ++index) {
            const auto &obj = objects[index];
            if (index > 0) {
                line << ",";
            }
            line << "{\"object_id\":" << obj.object_id << ",\"class_id\":" << obj.class_id
                 << ",\"confidence\":" << obj.confidence << ",\"bbox\":{\"left\":" << obj.left << ",\"top\":" << obj.top
                 << ",\"width\":" << obj.width << ",\"height\":" << obj.height
                 << "},\"accepted_for_detection\":" << (obj.accepted_for_detection ? "true" : "false")
                 << ",\"filter_reason\":\"" << obj.filter_reason << "\"}";
        }
        line << "]}\n";

        global_tracker_dump_stream << line.str();
        global_tracker_dump_stream.flush();
    }

    /**
     * @brief Filters out invalid ROIs (zero/negative dimensions, odd dimensions for YUV420).
     * @param width Width of the ROI
     * @param height Height of the ROI
     * @param left Left coordinate
     * @param top Top coordinate
     * @param filter_reason Human-readable reason when filtered
     * @return true if ROI should be filtered out, false otherwise
     */
    [[nodiscard]] bool should_filter_roi(const int width, const int height, const int left, const int top,
                                         const char **filter_reason) {
        // Filter out ROIs with zero or negative dimensions
        if (width <= 0 || height <= 0) {
            if (filter_reason) {
                *filter_reason = "non_positive_dimensions";
            }
            sf::debug(roi_detection_processing, "Skipping ROI with invalid dimensions: %dx%d at (%d,%d)", width, height,
                      left, top);
            return true;
        }

        // Filter out ROIs with odd dimensions for YUV420 processing (UV planes are subsampled by 2)
        if (width % 2 != 0 || height % 2 != 0) {
            if (filter_reason) {
                *filter_reason = "odd_dimensions";
            }
            sf::debug(roi_detection_processing,
                      "Skipping ROI with odd dimensions (YUV420 requires even): %dx%d at (%d,%d)", width, height, left,
                      top);
            return true;
        }

        if (filter_reason) {
            *filter_reason = "none";
        }
        return false;
    }

    /**
     * @brief Normalizes confidence values from different model ranges to 0.0-1.0.
     * @param confidence Raw confidence value
     * @return Normalized confidence value
     */
    [[nodiscard]] float normalize_confidence(const float confidence) {
        // YOLO models typically output 0-100, while other models output 0.0-1.0
        if (confidence > 1.0f) {
            return confidence / 100.0f; // Convert from 0-100 to 0.0-1.0
        }
        return confidence;
    }
} // namespace

/**
 * @brief Extracts detection results from DeepStream metadata.
 * @param buffer GstBuffer with DeepStream metadata attached.
 * @param data Global data with class filter configuration.
 * @return Vector of detections for the frame.
 */
std::vector<detection_result> get_detections_from_metadata(GstBuffer *buffer, global_data &data) {
    std::vector<detection_result> detections;
    int filtered_count = 0;
    const NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    if (!batch_meta) {
        sf::warning(roi_detection_processing, "Failed to get batch meta from buffer.");
        return detections;
    }

    for (const NvDsMetaList *frame = batch_meta->frame_meta_list; frame != nullptr; frame = frame->next) {
        const auto *frame_meta = static_cast<NvDsFrameMeta *>(frame->data);
        std::vector<tracker_object_dump> frame_tracker_dump;
        frame_tracker_dump.reserve(static_cast<size_t>(frame_meta->num_obj_meta));
        for (const NvDsMetaList *meta_obj = frame_meta->obj_meta_list; meta_obj != nullptr; meta_obj = meta_obj->next) {
            const auto *obj_meta = static_cast<NvDsObjectMeta *>(meta_obj->data);

            // Check class ID filter
            if (!data.pgie_class_ids_to_filter.empty() &&
                std::ranges::find(data.pgie_class_ids_to_filter, obj_meta->class_id) ==
                        data.pgie_class_ids_to_filter.end()) {
                continue;
            }

            // Extract bounding box from DeepStream's detector
            const int width = static_cast<int>(obj_meta->rect_params.width);
            const int height = static_cast<int>(obj_meta->rect_params.height);
            const int left = static_cast<int>(obj_meta->rect_params.left);
            const int top = static_cast<int>(obj_meta->rect_params.top);
            const char *filter_reason = "none";
            const bool filtered = should_filter_roi(width, height, left, top, &filter_reason);

            frame_tracker_dump.push_back(tracker_object_dump{
                    static_cast<uint64_t>(obj_meta->object_id),
                    static_cast<int>(obj_meta->class_id),
                    normalize_confidence(obj_meta->confidence),
                    left,
                    top,
                    width,
                    height,
                    !filtered,
                    filter_reason,
            });

            // Filter out invalid ROIs
            if (filtered) {
                filtered_count++;
                continue;
            }

            sf::rectangle bounding_box(left, top, width, height);
            const float normalized_confidence = normalize_confidence(obj_meta->confidence);

            detections.emplace_back(bounding_box, static_cast<int>(obj_meta->class_id), normalized_confidence,
                                    obj_meta->object_id);
        }
        maybe_dump_nvtracker_frame(data, *frame_meta, frame_tracker_dump);
    }

    pipeline_app_adapter(data).record_roi_filtering_update(filtered_count);

    return detections;
}
