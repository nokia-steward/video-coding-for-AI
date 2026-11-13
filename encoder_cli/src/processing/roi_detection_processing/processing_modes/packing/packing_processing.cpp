// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "packing_processing.h"
#include "../../../../../../libvcai/include/logging.h"
#include "../../../../bridge/app/pipeline_app_adapter.h"
#include "../../../../global_data.h"
#include "../../../../gst/input_types/frame_path_meta.h"
#include "../../../../roi/ctu_alignment.h"
#include "../../../../roi/pri_packer_view.h"
#include "../../../frame_processing_utils.h"
#include "../../../roi_detection_packing.h"
#include "../../../roi_detection_utils.h"

#include <functional>
#include <gst/gst.h>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

/**
 * @file packing_processing.cpp
 * @brief Implementation of packing processing mode.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    using rectangle_geometry_key = std::tuple<int, int, int, int>;

    [[nodiscard]] rectangle_geometry_key to_rectangle_geometry_key(const sf::rectangle &rect) {
        return {rect.left, rect.top, rect.width, rect.height};
    }

    [[nodiscard]] sf::rectangle rectangle_from_geometry_key(const rectangle_geometry_key &key) {
        return sf::rectangle(std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key));
    }

    [[nodiscard]] bool rectangles_overlap(const sf::rectangle &left, const sf::rectangle &right) {
        return !(left.left + left.width <= right.left || right.left + right.width <= left.left ||
                 left.top + left.height <= right.top || right.top + right.height <= left.top);
    }

    [[nodiscard]] bool rectangles_or_ctus_overlap(const sf::rectangle &left, const sf::rectangle &right) {
        if (rectangles_overlap(left, right)) {
            return true;
        }
        const auto left_ctu = compute_ctu_rectangle(left, default_ctu_size);
        const auto right_ctu = compute_ctu_rectangle(right, default_ctu_size);
        return ctu_rectangles_overlap(left_ctu, right_ctu);
    }

    /**
     * @brief Merges overlapping destination pairs until pixel and CTU overlap is eliminated.
     *
     * Overlap can still occur in rare fallback/edge paths when reconstructed copy destinations are
     * expanded or remapped after packing. This helper enforces a strict non-overlap invariant for
     * emitted metadata and copy destinations by collapsing overlap-connected components.
     */
    [[nodiscard]] normalized_copy_rectangles
    merge_overlapping_destination_pairs(const normalized_copy_rectangles &normalized, const frame_properties &props,
                                        const int frame_number) {
        if (normalized.destination_rectangles.size() < 2) {
            return normalized;
        }

        std::vector<sf::rectangle> current_sources = normalized.source_rectangles;
        std::vector<sf::rectangle> current_destinations = normalized.destination_rectangles;
        bool merged_any_components = false;

        while (current_destinations.size() > 1) {
            const size_t count = current_destinations.size();
            std::vector<size_t> parent(count);
            for (size_t index = 0; index < count; ++index) {
                parent[index] = index;
            }

            std::function<size_t(size_t)> find_root;
            find_root = [&parent, &find_root](const size_t index) -> size_t {
                if (parent[index] != index) {
                    parent[index] = find_root(parent[index]);
                }
                return parent[index];
            };
            const auto unite = [&parent, &find_root](const size_t left_index, const size_t right_index) {
                const size_t left_root = find_root(left_index);
                const size_t right_root = find_root(right_index);
                if (left_root != right_root) {
                    parent[right_root] = left_root;
                }
            };

            for (size_t left_index = 0; left_index < count; ++left_index) {
                for (size_t right_index = left_index + 1; right_index < count; ++right_index) {
                    if (rectangles_or_ctus_overlap(current_destinations[left_index],
                                                   current_destinations[right_index])) {
                        unite(left_index, right_index);
                    }
                }
            }

            std::unordered_map<size_t, std::vector<size_t>> groups;
            groups.reserve(count);
            for (size_t index = 0; index < count; ++index) {
                groups[find_root(index)].push_back(index);
            }
            if (groups.size() == count) {
                break;
            }

            merged_any_components = true;
            std::vector<sf::rectangle> merged_sources;
            std::vector<sf::rectangle> merged_destinations;
            merged_sources.reserve(groups.size());
            merged_destinations.reserve(groups.size());

            for (const auto &indices: groups | std::views::values) {
                if (indices.empty()) {
                    continue;
                }
                sf::rectangle merged_source = current_sources[indices[0]];
                sf::rectangle merged_destination = current_destinations[indices[0]];
                for (size_t position = 1; position < indices.size(); ++position) {
                    merged_source.expand_with(current_sources[indices[position]]);
                    merged_destination.expand_with(current_destinations[indices[position]]);
                }
                merged_sources.push_back(merged_source);
                merged_destinations.push_back(merged_destination);
            }

            current_sources = std::move(merged_sources);
            current_destinations = std::move(merged_destinations);
        }

        if (!merged_any_components) {
            return normalized;
        }

        auto merged_normalized =
                normalize_copy_rectangle_pairs(current_sources, current_destinations, props.width, props.height);
        sf::warning(roi_detection_processing,
                    "[Frame %d] Merged overlapping packed destinations: %zu -> %zu rectangles", frame_number,
                    normalized.destination_rectangles.size(), merged_normalized.destination_rectangles.size());
        return merged_normalized;
    }

    [[nodiscard]] normalized_copy_rectangles
    deduplicate_destination_pairs_strict(const normalized_copy_rectangles &normalized, const int frame_number) {
        const size_t pair_count =
                std::min(normalized.source_rectangles.size(), normalized.destination_rectangles.size());
        if (pair_count < 2U) {
            return normalized;
        }

        std::map<rectangle_geometry_key, sf::rectangle> destination_to_source_union;
        destination_to_source_union.clear();
        size_t duplicate_count = 0U;

        for (size_t index = 0; index < pair_count; ++index) {
            const auto key = to_rectangle_geometry_key(normalized.destination_rectangles[index]);
            if (!destination_to_source_union.contains(key)) {
                destination_to_source_union[key] = normalized.source_rectangles[index];
                continue;
            }

            destination_to_source_union[key].expand_with(normalized.source_rectangles[index]);
            ++duplicate_count;
        }

        if (duplicate_count == 0U) {
            return normalized;
        }

        std::vector<sf::rectangle> dedup_sources;
        std::vector<sf::rectangle> dedup_destinations;
        dedup_sources.reserve(destination_to_source_union.size());
        dedup_destinations.reserve(destination_to_source_union.size());

        for (const auto &[key, source_union]: destination_to_source_union) {
            dedup_destinations.push_back(rectangle_from_geometry_key(key));
            dedup_sources.push_back(source_union);
        }

        sf::warning(roi_detection_processing,
                    "[Frame %d] Strict destination dedup removed %zu duplicate destination pairs (%zu -> %zu)",
                    frame_number, duplicate_count, pair_count, dedup_destinations.size());

        return normalized_copy_rectangles{std::move(dedup_sources), std::move(dedup_destinations)};
    }

    /**
     * @brief Handles the case when no ROIs are detected.
     * @param data The global data structure.
     * @param input_buffer The input buffer.
     * @param output_buffer The output buffer.
     * @param props Frame properties for video copy.
     *
     * When no ROIs are detected, we copy the full input frame to the output (passthrough mode).
     * This ensures the video content is visible even when inference detects no objects.
     */
    void handle_no_rois_detected(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                 sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props) {
        const pipeline_app_adapter app_adapter(data);
        const auto frame_contract = get_frame_contract_from_buffer(input_buffer.get());
        if (!frame_contract.has_value()) {
            data.metadata_runtime.record_validation_failure();
            app_adapter.request_metadata_contract_fail_stop("packing no-roi frame missing contract metadata");
            sf::error(roi_detection_processing, "Cannot enqueue no-ROI metadata without frame contract");
            return;
        }

        sf::debug(roi_detection_processing, "No ROIs detected, pushing sentinel and copying full frame");
        // Push a sentinel value to keep the metadata queue synchronized with the frame queue
        data.packed_regions_queue.push(
                {.sequence_id = frame_contract->sequence_id, .regions = sf::packed_regions_info::create_sentinel()});
        data.metadata_runtime.record_bundle_enqueued();
        if (data.params.debug.draw_borders) {
            data.display_regions_queue.push({.sequence_id = frame_contract->sequence_id,
                                             .regions = sf::packed_regions_info::create_sentinel()});
            data.metadata_runtime.record_bundle_enqueued();
        }
        if (data.params.debug.save_frames) {
            data.debug_regions_queue.push({.sequence_id = frame_contract->sequence_id,
                                           .regions = sf::packed_regions_info::create_sentinel()});
            data.metadata_runtime.record_bundle_enqueued();
        }

        // Copy full video frame data (passthrough mode) so the frame is visible
        // even when no ROIs are detected. This prevents black frames.
        if (!copy_video_frame_data(input_buffer, output_buffer, props, &data.memory_runtime)) {
            sf::warning(roi_detection_processing, "Failed to copy video frame data in no-ROI mode, frame may be black");
        }

        // Copy timestamps and flags
        GST_BUFFER_PTS(output_buffer.get()) = GST_BUFFER_PTS(input_buffer.get());
        GST_BUFFER_DTS(output_buffer.get()) = GST_BUFFER_DTS(input_buffer.get());
        GST_BUFFER_DURATION(output_buffer.get()) = GST_BUFFER_DURATION(input_buffer.get());
        GST_BUFFER_OFFSET(output_buffer.get()) = GST_BUFFER_OFFSET(input_buffer.get());
        GST_BUFFER_OFFSET_END(output_buffer.get()) = GST_BUFFER_OFFSET_END(input_buffer.get());
        GST_BUFFER_FLAGS(output_buffer.get()) = GST_BUFFER_FLAGS(input_buffer.get());
    }
} // namespace

/**
 * @brief Processes a frame with ROIs using packing mode.
 * @param data Global data
 * @param input_buffer Input buffer
 * @param output_buffer Output buffer (may be modified)
 * @param props Frame properties
 * @param input_rectangles Detected rectangles
 * @param split_rectangles Split rectangles
 * @param is_static Whether this is static ROI mode
 * @param frame_number Frame number
 */
void packing_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                           sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                           const std::vector<sf::rectangle> &input_rectangles,
                           const std::vector<split_rectangle> &split_rectangles, const bool is_static,
                           const int frame_number) {
    // Perform packing (or get accumulated ROIs if background_blur is enabled)
    pri_packer_view packer(data.packer);
    const auto config = make_packing_config(data);
    const auto packing_result =
            perform_detection_packing(packer, config, input_rectangles, split_rectangles, is_static, frame_number);
    const auto &packed_rectangles = packing_result.packed_rectangles;
    const auto &original_rois = packing_result.original_rois;

    // Handle case where packer returns empty results
    if (packed_rectangles.empty()) {
        // Packer failed or returned empty - use passthrough mode
        // This can happen when all detected ROIs are filtered out
        sf::debug(roi_detection_processing, "Packer returned empty, using passthrough mode");
        handle_no_rois_detected(data, input_buffer, output_buffer, props);
        return;
    }

    // Use original accumulated ROIs for SEI message (not current frame's ROIs)
    // Note: packed_rectangles.size() may differ from input_rectangles.size() due to merging
    // This is expected behavior when the packer merges overlapping rectangles
    if (packed_rectangles.size() != input_rectangles.size()) {
        sf::info(roi_detection_processing,
                 "[Frame %d] Packer merged rectangles: input=%zu -> packed=%zu (expected when packer merges "
                 "overlapping ROIs)",
                 frame_number, input_rectangles.size(), packed_rectangles.size());
    }

    // For low_delay mode with merged ROIs, create unique packed positions to avoid copying the same
    // packed rectangle multiple times (which causes overlapping)
    // When multiple original ROIs map to the same packed position, compute the merged ROI (union) and copy once
    std::vector<sf::rectangle> unique_packed_rectangles;
    std::vector<sf::rectangle> unique_source_rectangles;

    // Check if merging occurred by seeing if any packed index appears multiple times in the mapping
    bool has_merging = false;
    if (!packing_result.original_to_packed_mapping.empty()) {
        std::map<size_t, size_t> packed_index_counts;
        for (const auto packed_idx: packing_result.original_to_packed_mapping) {
            packed_index_counts[packed_idx]++;
            if (packed_index_counts[packed_idx] > 1) {
                has_merging = true;
                break;
            }
        }
    }

    if (has_merging && !packing_result.original_to_packed_mapping.empty()) {
        // Build unique packed positions map (similar to random_access)
        // Key: packed_idx (the actual packed rectangle index from packer) to deduplicate positions
        // Value: (packed_rect, merged_source_rect) - compute merged ROI as union of all accumulated ROIs
        //        that map to this packed position
        std::map<size_t, std::pair<sf::rectangle, sf::rectangle>> unique_positions;
        for (size_t orig_idx = 0;
             orig_idx < packing_result.original_to_packed_mapping.size() && orig_idx < original_rois.size();
             ++orig_idx) {
            const size_t packed_idx = packing_result.original_to_packed_mapping[orig_idx];
            if (packed_idx >= packed_rectangles.size()) {
                sf::warning(roi_detection_processing,
                            "[Frame %d] Invalid packed index in mapping: orig_idx=%zu packed_idx=%zu "
                            "(packed_rectangles=%zu)",
                            frame_number, orig_idx, packed_idx, packed_rectangles.size());
                continue;
            }

            const auto &packed_rect = packed_rectangles[packed_idx];
            const auto &accumulated_roi = original_rois[orig_idx]; // Accumulated ROI for this original object

            if (!unique_positions.contains(packed_idx)) {
                // First ROI for this packed position - initialize with this accumulated ROI
                unique_positions[packed_idx] = std::make_pair(packed_rect, accumulated_roi);
            } else {
                // Multiple ROIs map to this packed position - compute union (merged ROI)
                auto &existing = unique_positions[packed_idx];
                existing.second.expand_with(accumulated_roi);

                // Verify that all packed rectangles with the same packed_idx have the same position
                // (they should, as expansion uses the same base position from packer)
                if (existing.first.left != packed_rect.left || existing.first.top != packed_rect.top) {
                    sf::warning(roi_detection_processing,
                                "[Frame %d] Packed rectangle %zu has different positions: existing=(%d,%d) vs "
                                "new=(%d,%d)",
                                frame_number, packed_idx, existing.first.left, existing.first.top, packed_rect.left,
                                packed_rect.top);
                }

                // Keep packed destination size from the packer.
                // Growing destination dimensions from merged source unions can create packed ROI overlaps.
            }
        }
        // Convert to vectors
        unique_packed_rectangles.reserve(unique_positions.size());
        unique_source_rectangles.reserve(unique_positions.size());
        for (const auto &entry: unique_positions | std::views::values) {
            unique_packed_rectangles.push_back(entry.first);
            unique_source_rectangles.push_back(entry.second);
        }
        sf::info(roi_detection_processing,
                 "[Frame %d] Created %zu unique packed positions from %zu expanded positions (merged ROIs)",
                 frame_number, unique_packed_rectangles.size(), packed_rectangles.size());
    } else {
        // No merging or no mapping - use as-is
        unique_packed_rectangles = packed_rectangles;
        unique_source_rectangles = original_rois;
    }

    auto normalized_copy_rectangles = normalize_copy_rectangle_pairs(unique_source_rectangles, unique_packed_rectangles,
                                                                     props.width, props.height);
    normalized_copy_rectangles = merge_overlapping_destination_pairs(normalized_copy_rectangles, props, frame_number);
    normalized_copy_rectangles = deduplicate_destination_pairs_strict(normalized_copy_rectangles, frame_number);

    if (normalized_copy_rectangles.destination_rectangles.empty()) {
        sf::warning(roi_detection_processing,
                    "[Frame %d] No valid effective ROI copy rectangles after normalization, using passthrough mode",
                    frame_number);
        handle_no_rois_detected(data, input_buffer, output_buffer, props);
        return;
    }

    dump_copy_rectangles_trace(config, frame_number, normalized_copy_rectangles.source_rectangles,
                               normalized_copy_rectangles.destination_rectangles);

    // Process frame (borders are now drawn in display branch, not here)
    sf::unique_gst_buffer_no_delete_ptr input_wrapper(input_buffer.get());
    create_metadata_and_process_frame(data, input_wrapper, output_buffer, props,
                                      normalized_copy_rectangles.source_rectangles,
                                      normalized_copy_rectangles.destination_rectangles, data.params.background_color);

    // Ensure timestamps are preserved (create_output_buffer copies them, but ensure they're still set)
    // Read fields atomically for thread-safety
    const GstClockTime pts = GST_BUFFER_PTS(input_buffer.get());
    const GstClockTime dts = GST_BUFFER_DTS(input_buffer.get());
    const GstClockTime duration = GST_BUFFER_DURATION(input_buffer.get());
    const guint64 offset = GST_BUFFER_OFFSET(input_buffer.get());
    const guint64 offset_end = GST_BUFFER_OFFSET_END(input_buffer.get());
    const guint flags = GST_BUFFER_FLAGS(input_buffer.get());

    GST_BUFFER_PTS(output_buffer.get()) = pts;
    GST_BUFFER_DTS(output_buffer.get()) = dts;
    GST_BUFFER_DURATION(output_buffer.get()) = duration;
    GST_BUFFER_OFFSET(output_buffer.get()) = offset;
    GST_BUFFER_OFFSET_END(output_buffer.get()) = offset_end;
    GST_BUFFER_FLAGS(output_buffer.get()) = flags;
}

/**
 * @brief Processes a frame with no ROIs using packing mode.
 * @param data Global data
 * @param input_buffer Input buffer
 * @param output_buffer Output buffer (may be modified)
 * @param props Frame properties
 */
void packing_process_no_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                             sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props) {
    handle_no_rois_detected(data, input_buffer, output_buffer, props);
}
