// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file roi_detection_packing.cpp
 * @brief Implements packing logic for detected ROIs.
 */

#include "roi_detection_packing.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include "../../libvcai/include/logging.h"
#include "../global_data.h"
#include "../gst/input_types/rtp_timestamp_overlay.h"
#include "../roi/pri_packer_view.h"
#include "roi_detection_processing/helpers.h"

/// GStreamer debug category for ROI detection processing.
GST_DEBUG_CATEGORY_STATIC(roi_detection_processing);
/// External declaration for the ROI detection processing debug category.
GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    std::mutex global_packing_trace_mutex;
    std::ofstream global_packing_trace_stream;
    bool global_packing_trace_initialized = false;
    bool global_packing_trace_failed = false;

    [[nodiscard]] const char *color_to_string(const sf::yuv_color color) {
        if (color == sf::yuv_color::red) {
            return "red";
        }
        if (color == sf::yuv_color::blue) {
            return "blue";
        }
        if (color == sf::yuv_color::purple) {
            return "purple";
        }
        if (color == sf::yuv_color::white) {
            return "white";
        }
        return "other";
    }

    [[nodiscard]] std::string escape_json_string(const std::string &value) {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char ch: value) {
            if (ch == '\\' || ch == '"') {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        return escaped;
    }

    [[nodiscard]] std::string rectangle_to_json(const sf::rectangle &rect) {
        std::ostringstream out;
        out << "{\"x\":" << rect.left << ",\"y\":" << rect.top << ",\"w\":" << rect.width << ",\"h\":" << rect.height
            << "}";
        return out.str();
    }

    [[nodiscard]] std::string rectangles_to_json(const std::vector<sf::rectangle> &rectangles) {
        std::ostringstream out;
        out << "[";
        for (size_t index = 0; index < rectangles.size(); ++index) {
            if (index > 0) {
                out << ",";
            }
            out << rectangle_to_json(rectangles[index]);
        }
        out << "]";
        return out.str();
    }

    [[nodiscard]] std::string split_rectangles_to_json(const std::vector<split_rectangle> &split_rectangles) {
        std::ostringstream out;
        out << "[";
        for (size_t index = 0; index < split_rectangles.size(); ++index) {
            if (index > 0) {
                out << ",";
            }
            const auto &split_rect = split_rectangles[index];
            out << "{\"bounds\":" << rectangle_to_json(split_rect.bounds)
                << ",\"original_object_id\":" << split_rect.original_object_id << ",\"sub_id\":" << split_rect.sub_id
                << ",\"color\":\"" << color_to_string(split_rect.color)
                << "\",\"was_split\":" << (split_rect.was_split ? "true" : "false") << "}";
        }
        out << "]";
        return out.str();
    }

    [[nodiscard]] std::string
    rectangles_with_ids_to_json(const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids) {
        std::ostringstream out;
        out << "[";
        for (size_t index = 0; index < rectangles_with_ids.size(); ++index) {
            if (index > 0) {
                out << ",";
            }
            out << "{\"rect\":" << rectangle_to_json(rectangles_with_ids[index].first) << ",\"id\":\""
                << escape_json_string(rectangles_with_ids[index].second) << "\"}";
        }
        out << "]";
        return out.str();
    }

    [[nodiscard]] std::string mapping_to_json(const std::vector<size_t> &mapping) {
        std::ostringstream out;
        out << "[";
        for (size_t index = 0; index < mapping.size(); ++index) {
            if (index > 0) {
                out << ",";
            }
            out << mapping[index];
        }
        out << "]";
        return out.str();
    }

    [[nodiscard]] bool ensure_packing_trace_stream(const packing_config &config) {
        if (global_packing_trace_initialized) {
            return global_packing_trace_stream.is_open();
        }
        if (global_packing_trace_failed) {
            return false;
        }

        const std::filesystem::path dump_path = config.frame_output_root / "tracker_dump" / "roi_packing_trace.jsonl";
        std::error_code create_error;
        std::filesystem::create_directories(dump_path.parent_path(), create_error);
        if (create_error) {
            sf::warning(roi_detection_processing, "Failed to create ROI packing trace directory '%s': %s",
                        dump_path.parent_path().string().c_str(), create_error.message().c_str());
            global_packing_trace_failed = true;
            return false;
        }

        global_packing_trace_stream.open(dump_path, std::ios::out | std::ios::trunc);
        if (!global_packing_trace_stream.is_open()) {
            sf::warning(roi_detection_processing, "Failed to open ROI packing trace file '%s'", dump_path.c_str());
            global_packing_trace_failed = true;
            return false;
        }

        global_packing_trace_initialized = true;
        sf::info(roi_detection_processing, "Temporary ROI packing trace enabled: %s", dump_path.c_str());
        return true;
    }

    void dump_packing_stage(const packing_config &config, const int frame_number, const char *stage,
                            const std::string &payload_json) {
        if (!config.packer_logging) {
            return;
        }

        std::lock_guard lock(global_packing_trace_mutex);
        if (!ensure_packing_trace_stream(config)) {
            return;
        }

        global_packing_trace_stream << "{\"frame_number\":" << frame_number << ",\"stage\":\"" << stage << "\"";
        if (!payload_json.empty()) {
            global_packing_trace_stream << "," << payload_json;
        }
        global_packing_trace_stream << "}\n";
        global_packing_trace_stream.flush();
    }

    void dump_packing_stage_force(const packing_config &config, const int frame_number, const char *stage,
                                  const std::string &payload_json) {
        std::lock_guard lock(global_packing_trace_mutex);
        if (!ensure_packing_trace_stream(config)) {
            return;
        }

        global_packing_trace_stream << "{\"frame_number\":" << frame_number << ",\"stage\":\"" << stage << "\"";
        if (!payload_json.empty()) {
            global_packing_trace_stream << "," << payload_json;
        }
        global_packing_trace_stream << "}\n";
        global_packing_trace_stream.flush();
    }

    /**
     * @brief Appends the RTP timestamp ROI to a packing result.
     *
     * The timestamp ROI occupies a fixed position in the frame (bottom-left corner).
     * It must be included in the PRI SEI metadata so that rtp_server copies the timestamp
     * pixels during frame reconstruction. The timestamp is drawn on the raw frame before
     * encoding, but without this ROI in the SEI the receiver would discard those pixels.
     *
     * The timestamp ROI uses the same source and packed position (identity mapping) because
     * the timestamp text is always drawn at its final location -- it is not repacked.
     *
     * Filtering of the timestamp ROI for border drawing is handled separately in the
     * display branch (display_branch.cpp).
     *
     * @param result Packing result to append the timestamp ROI to.
     * @param config Narrow packing configuration (draw_rtp_timestamp flag, frame dimensions).
     */
    void append_timestamp_roi(packing_result &result, const packing_config &config) {
        if (!config.draw_rtp_timestamp) {
            return;
        }

        const sf::rectangle timestamp_roi = create_timestamp_roi_rectangle(config.width, config.height);
        if (!timestamp_roi.is_valid()) {
            return;
        }

        // Append at the end -- the timestamp ROI maps to itself (identity: same source and packed position)
        const size_t insert_index = result.packed_rectangles.size();
        result.packed_rectangles.push_back(timestamp_roi);
        result.original_rois.push_back(timestamp_roi);
        result.is_predicted.push_back(false);

        if (!result.original_to_packed_mapping.empty()) {
            result.original_to_packed_mapping.push_back(insert_index);
        }
    }
    /**
     * @brief Converts input rectangles to rectangles with IDs for inference mode.
     * @param input_rectangles Input rectangles
     * @param split_rectangles Split rectangles with object IDs
     * @return Vector of rectangles with IDs
     */
    [[nodiscard]] std::vector<std::pair<sf::rectangle, std::string>>
    create_rectangles_with_ids_inference(const std::vector<sf::rectangle> &input_rectangles,
                                         const std::vector<split_rectangle> &split_rectangles) {
        std::vector<std::pair<sf::rectangle, std::string>> rectangles_with_ids;
        rectangles_with_ids.reserve(input_rectangles.size());

        if (!split_rectangles.empty() && split_rectangles.size() == input_rectangles.size()) {
            // Direct mapping: split_rectangles and input_rectangles should be in same order
            for (size_t i = 0; i < input_rectangles.size(); ++i) {
                rectangles_with_ids.emplace_back(input_rectangles[i],
                                                 std::to_string(split_rectangles[i].original_object_id));
            }
        } else {
            // Fallback: match by bounds
            for (const auto &rect: input_rectangles) {
                bool found = false;
                for (const auto &split_rect: split_rectangles) {
                    if (split_rect.bounds.left == rect.left && split_rect.bounds.top == rect.top &&
                        split_rect.bounds.width == rect.width && split_rect.bounds.height == rect.height) {
                        rectangles_with_ids.emplace_back(rect, std::to_string(split_rect.original_object_id));
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Fallback: use a generated ID if no match found
                    rectangles_with_ids.emplace_back(rect, "unknown_" + std::to_string(rectangles_with_ids.size()));
                }
            }
        }
        return rectangles_with_ids;
    }

    /**
     * @brief Calls the packer to get packed rectangles.
     * @param packer Non-owning view over the active PRI packer.
     * @param rectangles_with_ids Rectangles with IDs
     * @param frame_number Frame number
     * @return Optional pack result
     */
    [[nodiscard]] std::optional<pack_result>
    call_packer(pri_packer_view &packer, const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                const int frame_number) {
        if (packer.is_random_access()) {
            // random_access_packer uses add_frame/get_complete_period, not
            // get_packed_rectangles_with_ids
            sf::debug(roi_detection_processing, "Skipping packer call for random_access_packer (uses "
                                                "add_frame/get_complete_period)");
            return std::nullopt;
        }

        sf::debug(roi_detection_processing,
                  "Calling packer->get_packed_rectangles_with_ids for frame %d with %zu rectangles", frame_number,
                  rectangles_with_ids.size());
        return packer.pack_low_delay(rectangles_with_ids, frame_number);
    }

    /**
     * @brief Creates fallback packer and calls it.
     * @param input_rectangles Input rectangles
     * @param width Frame width
     * @param height Frame height
     * @param num_frames Number of frames for accumulation
     * @param frame_number Frame number
     * @return Optional pack result
     */
    [[nodiscard]] std::optional<pack_result> create_fallback_packer(const std::vector<sf::rectangle> &input_rectangles,
                                                                    const int width, const int height,
                                                                    const int num_frames, const int frame_number) {
        low_delay_packer fallback_packer(width, height, 64, num_frames);
        // Convert input_rectangles to rectangles_with_ids format (use index as ID)
        std::vector<std::pair<sf::rectangle, std::string>> fallback_rects_with_ids;
        fallback_rects_with_ids.reserve(input_rectangles.size());
        for (size_t i = 0; i < input_rectangles.size(); ++i) {
            fallback_rects_with_ids.emplace_back(input_rectangles[i], std::to_string(i));
        }
        return fallback_packer.get_packed_rectangles_with_ids(fallback_rects_with_ids, frame_number);
    }

    /**
     * @brief Clamps a rectangle to fit within frame bounds.
     * @param rect Rectangle to clamp
     * @param max_x Maximum x coordinate
     * @param max_y Maximum y coordinate
     * @param rect_w Desired width
     * @param rect_h Desired height
     * @return Clamped rectangle
     */
    [[nodiscard]] sf::rectangle clamp_rectangle(const sf::rectangle &rect, const int max_x, const int max_y,
                                                const int rect_w, const int rect_h) {
        const int clamped_x = std::max(0, std::min(rect.left, max_x - rect_w));
        const int clamped_y = std::max(0, std::min(rect.top, max_y - rect_h));
        const int clamped_w = std::min(rect_w, max_x - clamped_x);
        const int clamped_h = std::min(rect_h, max_y - clamped_y);
        return sf::rectangle(clamped_x, clamped_y, clamped_w, clamped_h);
    }

    /**
     * @brief Expands packed rectangles using mapping (for merged ROIs).
     * @param packed_result Pack result with mapping
     * @param input_rectangles Input rectangles
     * @param max_x Maximum x coordinate
     * @param max_y Maximum y coordinate
     * @return Vector of expanded packed rectangles
     */
    [[nodiscard]] std::vector<sf::rectangle>
    expand_packed_rectangles_with_mapping(const pack_result &packed_result,
                                          const std::vector<sf::rectangle> &input_rectangles, const int max_x,
                                          const int max_y) {
        std::vector<sf::rectangle> packed_rectangles;
        packed_rectangles.reserve(packed_result.sorted_source_rectangles.size());

        for (size_t i = 0; i < packed_result.sorted_source_rectangles.size(); ++i) {
            const size_t packed_idx = packed_result.original_to_packed_mapping[i];

            if (packed_idx < packed_result.packed_rectangles.size()) {
                const auto &packed_rect = packed_result.packed_rectangles[packed_idx];
                // Use current frame's ROI dimensions if available, otherwise use accumulated dimensions
                int rect_w, rect_h;
                if (i < input_rectangles.size()) {
                    rect_w = input_rectangles[i].width;
                    rect_h = input_rectangles[i].height;
                } else {
                    const auto &original_rect = packed_result.sorted_source_rectangles[i];
                    rect_w = original_rect.width;
                    rect_h = original_rect.height;
                }

                const auto clamped = clamp_rectangle(packed_rect, max_x, max_y, rect_w, rect_h);
                if (clamped.width > 0 && clamped.height > 0) {
                    packed_rectangles.push_back(clamped);
                } else {
                    sf::warning(roi_detection_processing,
                                "Packed ROI %zu would be out of bounds, using original position", i);
                    if (i < input_rectangles.size()) {
                        packed_rectangles.push_back(input_rectangles[i]);
                    } else {
                        packed_rectangles.push_back(packed_result.sorted_source_rectangles[i]);
                    }
                }
            } else {
                sf::warning(roi_detection_processing,
                            "Invalid packed index %zu for original ROI %zu, using original position", packed_idx, i);
                if (i < input_rectangles.size()) {
                    packed_rectangles.push_back(input_rectangles[i]);
                } else {
                    packed_rectangles.push_back(packed_result.sorted_source_rectangles[i]);
                }
            }
        }
        return packed_rectangles;
    }

    /**
     * @brief Expands packed rectangles assuming 1:1 mapping (no merging).
     * @param packed_result Pack result
     * @param input_rectangles Input rectangles
     * @param max_x Maximum x coordinate
     * @param max_y Maximum y coordinate
     * @return Vector of expanded packed rectangles
     */
    [[nodiscard]] std::vector<sf::rectangle>
    expand_packed_rectangles_1to1(const pack_result &packed_result, const std::vector<sf::rectangle> &input_rectangles,
                                  const int max_x, const int max_y) {
        std::vector<sf::rectangle> packed_rectangles;
        packed_rectangles.reserve(packed_result.packed_rectangles.size());

        for (size_t i = 0; i < packed_result.packed_rectangles.size(); ++i) {
            const auto &packed_rect = packed_result.packed_rectangles[i];
            // Use current frame's ROI dimensions if available, otherwise use packed dimensions
            int rect_w, rect_h;
            if (i < input_rectangles.size()) {
                rect_w = input_rectangles[i].width;
                rect_h = input_rectangles[i].height;
            } else {
                rect_w = packed_rect.width;
                rect_h = packed_rect.height;
            }

            const auto clamped = clamp_rectangle(packed_rect, max_x, max_y, rect_w, rect_h);
            if (clamped.width > 0 && clamped.height > 0) {
                packed_rectangles.push_back(clamped);
            } else {
                sf::warning(roi_detection_processing, "Packed ROI %zu would be out of bounds, using original position",
                            i);
                if (i < input_rectangles.size()) {
                    packed_rectangles.push_back(input_rectangles[i]);
                } else {
                    packed_rectangles.push_back(packed_rect);
                }
            }
        }
        return packed_rectangles;
    }

    /**
     * @brief Expands packed rectangles from pack result.
     * @param packed_result Pack result
     * @param input_rectangles Input rectangles
     * @param max_x Maximum x coordinate
     * @param max_y Maximum y coordinate
     * @return Vector of expanded packed rectangles
     */
    [[nodiscard]] std::vector<sf::rectangle>
    expand_packed_rectangles(const pack_result &packed_result, const std::vector<sf::rectangle> &input_rectangles,
                             const int max_x, const int max_y) {
        // Standard case: sorted_source_rectangles matches input_rectangles (or 1:1 mapping)
        if (packed_result.original_to_packed_mapping.empty() ||
            packed_result.original_to_packed_mapping.size() != packed_result.sorted_source_rectangles.size()) {
            // No mapping provided or size mismatch - assume 1:1 mapping (no merging)
            if (packed_result.sorted_source_rectangles.size() == packed_result.packed_rectangles.size()) {
                return expand_packed_rectangles_1to1(packed_result, input_rectangles, max_x, max_y);
            }
            sf::error(roi_detection_processing,
                      "Packing result has size mismatch and no mapping provided: %zu input -> %zu packed",
                      packed_result.sorted_source_rectangles.size(), packed_result.packed_rectangles.size());
            return input_rectangles; // Fallback: use original positions
        }
        // Use the mapping to expand packed_rectangles to match input_rectangles
        return expand_packed_rectangles_with_mapping(packed_result, input_rectangles, max_x, max_y);
    }
} // namespace

packing_result perform_detection_packing(pri_packer_view &packer, const packing_config &config,
                                         const std::vector<sf::rectangle> &input_rectangles,
                                         const std::vector<split_rectangle> &split_rectangles, const bool is_static,
                                         const int frame_number) {
    // Detection results are used directly -- no filtering of the timestamp ROI.
    // The timestamp ROI is appended to the final result so that it appears in the PRI SEI
    // and its pixels are copied to the packed frame for the receiver to reconstruct.
    const bool should_apply_fixed_margin = config.pack_rois && !config.is_background_processing_enabled() &&
                                           config.roi_fixed_margin_px > 0 &&
                                           config.roi_margin_mode_value != roi_margin_mode::off;
    const bool placement_only_margin =
            should_apply_fixed_margin && config.roi_margin_mode_value == roi_margin_mode::placement;
    const bool content_margin = should_apply_fixed_margin && config.roi_margin_mode_value == roi_margin_mode::content;

    const auto expanded_rectangles =
            should_apply_fixed_margin
                    ? apply_fixed_roi_margin(input_rectangles, config.roi_fixed_margin_px, config.width, config.height)
                    : input_rectangles;
    const auto &packing_rectangles = should_apply_fixed_margin ? expanded_rectangles : input_rectangles;
    const auto &content_rectangles = content_margin ? expanded_rectangles : input_rectangles;
    const auto &packing_split_rectangles = split_rectangles;

    {
        std::ostringstream payload;
        payload << "\"is_static\":" << (is_static ? "true" : "false")
                << ",\"should_apply_fixed_margin\":" << (should_apply_fixed_margin ? "true" : "false")
                << ",\"placement_only_margin\":" << (placement_only_margin ? "true" : "false")
                << ",\"content_margin\":" << (content_margin ? "true" : "false")
                << ",\"input_rectangles\":" << rectangles_to_json(input_rectangles)
                << ",\"split_rectangles\":" << split_rectangles_to_json(split_rectangles)
                << ",\"expanded_rectangles\":" << rectangles_to_json(expanded_rectangles)
                << ",\"packing_rectangles\":" << rectangles_to_json(packing_rectangles)
                << ",\"content_rectangles\":" << rectangles_to_json(content_rectangles);
        dump_packing_stage(config, frame_number, "pre_pack_setup", payload.str());
    }
    // When background processing is enabled, we still need to call the packer to get accumulated ROIs
    // but we won't actually pack them (they'll be used for blurring instead)
    const bool should_pack = config.pack_rois && !config.is_background_processing_enabled();

    if (!should_pack && !config.is_background_processing_enabled()) {
        sf::debug(roi_detection_processing, "Packing DISABLED - keeping original detection positions");
        // Track prediction status for non-packed case
        std::vector<bool> is_predicted;
        is_predicted.reserve(packing_split_rectangles.size());
        for (const auto &split_rect: packing_split_rectangles) {
            is_predicted.push_back(split_rect.color == sf::yuv_color::red);
        }
        // Create 1:1 mapping for non-packed case
        std::vector<size_t> mapping;
        mapping.reserve(packing_rectangles.size());
        for (size_t i = 0; i < packing_rectangles.size(); ++i) {
            mapping.push_back(i);
        }
        packing_result result{content_rectangles, content_rectangles, std::move(mapping), std::move(is_predicted)};
        append_timestamp_roi(result, config);
        {
            std::ostringstream payload;
            payload << "\"reason\":\"packing_disabled\""
                    << ",\"packed_rectangles\":" << rectangles_to_json(result.packed_rectangles)
                    << ",\"original_rois\":" << rectangles_to_json(result.original_rois)
                    << ",\"mapping\":" << mapping_to_json(result.original_to_packed_mapping);
            dump_packing_stage(config, frame_number, "final_output", payload.str());
        }
        return result;
    }

    // When background processing is combined with static/groundtruth mode, the packer's
    // cross-frame accumulation is unnecessary: static ROIs are identical every frame
    // and groundtruth ROIs use frame-unique IDs that prevent meaningful accumulation.
    // Skip the packer entirely and return the input rectangles directly to avoid the
    // expensive (and always-failing) packing attempt.
    if (config.is_background_processing_enabled() && is_static) {
        sf::debug(roi_detection_processing,
                  "Background processing + static/groundtruth mode - skipping packer, using original ROIs");
        std::vector<size_t> mapping;
        mapping.reserve(packing_rectangles.size());
        for (size_t i = 0; i < packing_rectangles.size(); ++i) {
            mapping.push_back(i);
        }
        std::vector<bool> is_predicted(packing_rectangles.size(), false);
        packing_result result{content_rectangles, content_rectangles, std::move(mapping), std::move(is_predicted)};
        append_timestamp_roi(result, config);
        {
            std::ostringstream payload;
            payload << "\"reason\":\"background_mode_static_skip_packer\""
                    << ",\"packed_rectangles\":" << rectangles_to_json(result.packed_rectangles)
                    << ",\"original_rois\":" << rectangles_to_json(result.original_rois)
                    << ",\"mapping\":" << mapping_to_json(result.original_to_packed_mapping);
            dump_packing_stage(config, frame_number, "final_output", payload.str());
        }
        return result;
    }

    if (config.is_background_processing_enabled()) {
        sf::debug(roi_detection_processing,
                  "Background mode '%s' enabled - using packer for ROI accumulation (no packing)",
                  background_mode_to_string(config.effective_background_mode_value));
    } else {
        sf::debug(roi_detection_processing, "Packing enabled - using packer algorithm for detections");
    }

    // Track prediction status for each rectangle
    std::vector<bool> is_predicted;
    is_predicted.reserve(packing_split_rectangles.size());
    size_t predicted_count = 0;
    size_t non_predicted_count = 0;
    for (const auto &split_rect: packing_split_rectangles) {
        const bool is_pred = split_rect.color == sf::yuv_color::red;
        is_predicted.push_back(is_pred);
        if (is_pred) {
            predicted_count++;
            sf::debug(roi_detection_processing, "[Frame %d] Packing: split_rect obj_id=%u is PREDICTED (color=RED)",
                      frame_number, split_rect.original_object_id);
        } else {
            non_predicted_count++;
            sf::debug(roi_detection_processing, "[Frame %d] Packing: split_rect obj_id=%u is DETECTED (color=%s)",
                      frame_number, split_rect.original_object_id,
                      split_rect.color == sf::yuv_color::purple ? "PURPLE" : "BLUE");
        }
    }
    sf::info(roi_detection_processing,
             "[Frame %d] Packing: Tracking prediction status - %zu predicted (RED), %zu detected (BLUE/PURPLE)",
             frame_number, predicted_count, non_predicted_count);

    // Convert input rectangles to rectangles with object IDs
    std::vector<std::pair<sf::rectangle, std::string>> rectangles_with_ids;
    if (is_static) {
        // For static/groundtruth mode, create IDs that include the frame number.
        // This prevents the low_delay_packer from accumulating (expand_with) ROIs
        // across frames via roi_histories.  Static ROIs are identical every frame
        // so accumulation is harmless for them, but groundtruth ROIs change per
        // frame (different objects at different positions), and index-only IDs
        // like "0" would cause the packer to union unrelated bounding boxes from
        // different frames into huge rectangles.
        rectangles_with_ids.reserve(packing_rectangles.size());
        for (size_t i = 0; i < packing_rectangles.size(); ++i) {
            rectangles_with_ids.emplace_back(packing_rectangles[i],
                                             std::to_string(frame_number) + "_" + std::to_string(i));
        }
        // For static/groundtruth mode, assume all are non-predicted (blue)
        is_predicted.assign(packing_rectangles.size(), false);
    } else {
        rectangles_with_ids = create_rectangles_with_ids_inference(packing_rectangles, packing_split_rectangles);
    }
    {
        std::ostringstream payload;
        payload << "\"rectangles_with_ids\":" << rectangles_with_ids_to_json(rectangles_with_ids);
        dump_packing_stage(config, frame_number, "rectangles_with_ids", payload.str());
    }

    // Call the packer
    std::optional<pack_result> packed_result_opt = call_packer(packer, rectangles_with_ids, frame_number);

    // Fallback to low_delay_packer if packer not available
    if (!packed_result_opt) {
        sf::warning(roi_detection_processing, "Packer not available, falling back to low_delay_packer");
        packed_result_opt = create_fallback_packer(packing_rectangles, config.width, config.height, config.num_frames,
                                                   frame_number);
        // For fallback, create 1:1 mapping
        if (packed_result_opt) {
            std::vector<size_t> fallback_mapping;
            fallback_mapping.reserve(packing_rectangles.size());
            for (size_t i = 0; i < packing_rectangles.size(); ++i) {
                fallback_mapping.push_back(i);
            }
            packed_result_opt->original_to_packed_mapping = std::move(fallback_mapping);
        }
    }

    if (packed_result_opt.has_value()) {
        std::ostringstream payload;
        payload << "\"sorted_source_rectangles\":" << rectangles_to_json(packed_result_opt->sorted_source_rectangles)
                << ",\"packed_rectangles\":" << rectangles_to_json(packed_result_opt->packed_rectangles)
                << ",\"result_size\":" << rectangle_to_json(packed_result_opt->result_size)
                << ",\"mapping\":" << mapping_to_json(packed_result_opt->original_to_packed_mapping);
        dump_packing_stage(config, frame_number, "packer_result_raw", payload.str());
    } else {
        dump_packing_stage(config, frame_number, "packer_result_raw", "\"result\":\"nullopt\"");
    }

    // Handle packing failure
    if (!packed_result_opt) {
        sf::error(roi_detection_processing,
                  "ERROR: Failed to pack detected ROIs for frame %d. They do not fit in the frame "
                  "dimensions (%dx%d). "
                  "Using original positions.",
                  frame_number, config.width, config.height);
        // Create 1:1 mapping for failure case
        std::vector<size_t> mapping;
        mapping.reserve(packing_rectangles.size());
        for (size_t i = 0; i < packing_rectangles.size(); ++i) {
            mapping.push_back(i);
        }
        packing_result result{packing_rectangles, packing_rectangles, std::move(mapping), std::move(is_predicted)};
        append_timestamp_roi(result, config);
        {
            std::ostringstream payload;
            payload << "\"reason\":\"packing_failed\""
                    << ",\"packed_rectangles\":" << rectangles_to_json(result.packed_rectangles)
                    << ",\"original_rois\":" << rectangles_to_json(result.original_rois)
                    << ",\"mapping\":" << mapping_to_json(result.original_to_packed_mapping);
            dump_packing_stage(config, frame_number, "final_output", payload.str());
        }
        return result;
    }

    const auto packed_result = std::move(*packed_result_opt);

    // Check if packer returned empty result (e.g., random_access_packer still buffering)
    if (packed_result.sorted_source_rectangles.empty() && packed_result.packed_rectangles.empty()) {
        sf::debug(roi_detection_processing, "Packer returned empty result (likely still buffering), returning empty");
        packing_result result{{}, {}, {}, {}};
        append_timestamp_roi(result, config);
        dump_packing_stage(config, frame_number, "final_output", "\"reason\":\"packer_buffering_empty\"");
        return result;
    }

    sf::debug(roi_detection_processing, "Detection packing successful! Result size: %dx%d",
              packed_result.result_size.width, packed_result.result_size.height);
    sf::debug(roi_detection_processing, "Packed detection ROIs:");
    for (size_t i = 0; i < packed_result.packed_rectangles.size(); ++i) {
        const auto &rect = packed_result.packed_rectangles[i];
        sf::debug(roi_detection_processing, "  Packed detection ROI %zu: %dx%d at (%d,%d)", i, rect.width, rect.height,
                  rect.left, rect.top);
    }

    // Expand packed rectangles to match input_rectangles size
    const auto packed_rectangles =
            expand_packed_rectangles(packed_result, content_rectangles, config.width, config.height);

    // Use the packer's mapping, or create 1:1 mapping if not available
    std::vector<size_t> mapping;
    if (!packed_result.original_to_packed_mapping.empty() &&
        packed_result.original_to_packed_mapping.size() == packed_result.sorted_source_rectangles.size()) {
        mapping = packed_result.original_to_packed_mapping;
        sf::info(roi_detection_processing,
                 "[Frame %d] Packing: Using packer mapping - %zu original -> %zu packed rectangles", frame_number,
                 mapping.size(), packed_rectangles.size());
        // Log merge information
        std::vector<size_t> merge_counts(packed_rectangles.size(), 0);
        for (const auto orig_to_packed: mapping) {
            if (orig_to_packed < merge_counts.size()) {
                merge_counts[orig_to_packed]++;
            }
        }
        for (size_t packed_idx = 0; packed_idx < merge_counts.size(); ++packed_idx) {
            if (merge_counts[packed_idx] > 1) {
                sf::info(roi_detection_processing,
                         "[Frame %d] Packing: Packed rectangle %zu is MERGED from %zu original rectangles",
                         frame_number, packed_idx, merge_counts[packed_idx]);
            }
        }
    } else {
        // Create 1:1 mapping if packer didn't provide one
        mapping.reserve(packed_rectangles.size());
        for (size_t i = 0; i < packed_rectangles.size(); ++i) {
            mapping.push_back(i);
        }
        sf::debug(roi_detection_processing, "[Frame %d] Packing: No packer mapping available, using 1:1 mapping",
                  frame_number);
    }

    // When background mode is enabled, return accumulated ROIs as both packed and original
    // (since we're not actually packing, just using accumulated ROIs for blurring)
    if (config.is_background_processing_enabled()) {
        sf::debug(roi_detection_processing,
                  "[Frame %d] Background mode '%s' enabled - using accumulated ROIs without packing", frame_number,
                  background_mode_to_string(config.effective_background_mode_value));
        // Create 1:1 mapping for accumulated ROIs
        std::vector<size_t> blur_mapping;
        blur_mapping.reserve(packed_result.sorted_source_rectangles.size());
        for (size_t i = 0; i < packed_result.sorted_source_rectangles.size(); ++i) {
            blur_mapping.push_back(i);
        }
        packing_result result{packed_result.sorted_source_rectangles, packed_result.sorted_source_rectangles,
                              std::move(blur_mapping), std::move(is_predicted)};
        append_timestamp_roi(result, config);
        {
            std::ostringstream payload;
            payload << "\"reason\":\"background_mode_accumulated\""
                    << ",\"packed_rectangles\":" << rectangles_to_json(result.packed_rectangles)
                    << ",\"original_rois\":" << rectangles_to_json(result.original_rois)
                    << ",\"mapping\":" << mapping_to_json(result.original_to_packed_mapping);
            dump_packing_stage(config, frame_number, "final_output", payload.str());
        }
        return result;
    }

    const auto &source_rois_for_output =
            placement_only_margin ? content_rectangles : packed_result.sorted_source_rectangles;

    // Return both packed rectangles and original accumulated ROIs
    // In placement-only margin mode, source ROIs remain non-dilated.
    packing_result result{std::move(packed_rectangles), source_rois_for_output, std::move(mapping),
                          std::move(is_predicted)};
    append_timestamp_roi(result, config);
    {
        std::ostringstream payload;
        payload << "\"reason\":\"packed\""
                << ",\"packed_rectangles\":" << rectangles_to_json(result.packed_rectangles)
                << ",\"original_rois\":" << rectangles_to_json(result.original_rois)
                << ",\"mapping\":" << mapping_to_json(result.original_to_packed_mapping);
        dump_packing_stage(config, frame_number, "final_output", payload.str());
    }
    return result;
}

void dump_packing_trace_stage(const packing_config &config, const int frame_number, const std::string_view stage,
                              const std::string &payload_json) {
    if (stage.empty()) {
        return;
    }
    dump_packing_stage(config, frame_number, stage.data(), payload_json);
}

void dump_copy_rectangles_trace(const packing_config &config, const int frame_number,
                                const std::vector<sf::rectangle> &source_rectangles,
                                const std::vector<sf::rectangle> &destination_rectangles) {
    const size_t pair_count = std::min(source_rectangles.size(), destination_rectangles.size());
    const bool size_mismatch = source_rectangles.size() != destination_rectangles.size();

    std::ostringstream payload;
    payload << "\"source_rectangles\":" << rectangles_to_json(source_rectangles)
            << ",\"destination_rectangles\":" << rectangles_to_json(destination_rectangles)
            << ",\"size_mismatch\":" << (size_mismatch ? "true" : "false") << ",\"pairs\":[";
    for (size_t index = 0; index < pair_count; ++index) {
        if (index > 0) {
            payload << ",";
        }
        payload << "{\"source\":" << rectangle_to_json(source_rectangles[index])
                << ",\"destination\":" << rectangle_to_json(destination_rectangles[index]) << "}";
    }
    payload << "]";
    dump_packing_trace_stage(config, frame_number, "copy_rectangles", payload.str());
}

void dump_pri_regions_trace(const packing_config &config, const int frame_number, const uint64_t pts,
                            const bool pts_valid, const uint64_t dts, const bool dts_valid,
                            const std::vector<sf::rectangle> &target_rectangles,
                            const std::vector<sf::rectangle> &packed_rectangles) {
    const size_t pair_count = std::min(target_rectangles.size(), packed_rectangles.size());
    const bool size_mismatch = target_rectangles.size() != packed_rectangles.size();

    std::set<std::string> unique_pair_keys;
    size_t duplicate_pair_count = 0U;

    std::ostringstream payload;
    payload << "\"pts\":";
    if (pts_valid) {
        payload << pts;
    } else {
        payload << "null";
    }

    payload << ",\"dts\":";
    if (dts_valid) {
        payload << dts;
    } else {
        payload << "null";
    }

    payload << ",\"target_rectangles\":" << rectangles_to_json(target_rectangles)
            << ",\"packed_rectangles\":" << rectangles_to_json(packed_rectangles)
            << ",\"size_mismatch\":" << (size_mismatch ? "true" : "false") << ",\"regions\":[";

    for (size_t index = 0; index < pair_count; ++index) {
        if (index > 0) {
            payload << ",";
        }

        const auto &target = target_rectangles[index];
        const auto &packed = packed_rectangles[index];
        const std::string pair_key = std::to_string(packed.left) + ":" + std::to_string(packed.top) + ":" +
                                     std::to_string(packed.width) + ":" + std::to_string(packed.height) + "|" +
                                     std::to_string(target.left) + ":" + std::to_string(target.top) + ":" +
                                     std::to_string(target.width) + ":" + std::to_string(target.height);
        if (!unique_pair_keys.insert(pair_key).second) {
            ++duplicate_pair_count;
        }

        payload << "{\"packed\":" << rectangle_to_json(packed) << ",\"target\":" << rectangle_to_json(target)
                << ",\"pair_key\":\"" << pair_key << "\"}";
    }
    payload << "],\"duplicate_pair_count\":" << duplicate_pair_count;

    dump_packing_stage_force(config, frame_number, "pri_regions", payload.str());
}

packing_config make_packing_config(const global_data &data) {
    packing_config config;
    config.pack_rois = data.params.pack_rois;
    config.effective_background_mode_value = data.params.effective_background_mode_value;
    config.roi_fixed_margin_px = data.params.roi_fixed_margin_px;
    config.roi_margin_mode_value = data.params.roi_margin_mode_value;
    config.width = data.params.width;
    config.height = data.params.height;
    config.num_frames = data.params.num_frames;
    config.draw_rtp_timestamp = data.params.debug.draw_rtp_timestamp;
    config.packer_logging = data.params.packer_logging;
    config.frame_output_root = resolve_frame_output_root_directory(data.params);
    return config;
}
