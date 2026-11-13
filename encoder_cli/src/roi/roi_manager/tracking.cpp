// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "tracking.h"
#include "../../../../libvcai/include/logging.h"
#include "../../encoder_enums.h"
#include "../../processing/roi_detection_processing.h"
#include "../roi_manager.h"
#include "prediction.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @file tracking.cpp
 * @brief Implementation of object tracking algorithms for ROI manager.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

namespace {
    /**
     * @brief Maximum number of history entries to keep per object.
     * This prevents unbounded memory growth while keeping enough history for velocity calculations.
     */
    constexpr size_t MAX_HISTORY_SIZE = 100;

    /**
     * @brief Trims object history to prevent unbounded growth.
     * Keeps the most recent MAX_HISTORY_SIZE entries.
     * @param obj The object whose history should be trimmed.
     */
    void trim_history_if_needed(detected_object &obj) {
        if (obj.history.size() > MAX_HISTORY_SIZE) {
            // Keep only the most recent entries
            const size_t entries_to_remove = obj.history.size() - MAX_HISTORY_SIZE;
            obj.history.erase(obj.history.begin(), obj.history.begin() + static_cast<ptrdiff_t>(entries_to_remove));
        }
        // Note: We don't call reserve() here to avoid potential reallocations that could trigger
        // crashes if the heap is already corrupted. The vector will grow naturally as needed.
    }

    /**
     * @brief Gets the last ROI from a tracked object's history.
     * @param obj The tracked object.
     * @return The last ROI bounding box, or empty rectangle if no history.
     */
    [[nodiscard]] sf::rectangle get_last_roi(const detected_object &obj) {
        if (obj.history.empty()) {
            return sf::rectangle{0, 0, 0, 0};
        }
        return obj.history.back().bounds;
    }

    /**
     * @brief Estimates the recent velocity of an object based on history.
     * @param obj The tracked object.
     * @return Velocity vector (x, y) in pixels per frame.
     */
    [[nodiscard]] std::pair<float, float> estimate_recent_velocity(const detected_object &obj) {
        if (obj.history.size() < 2) {
            return {0.0f, 0.0f};
        }

        const auto &latest = obj.history.back().bounds;
        const auto &previous = obj.history[obj.history.size() - 2].bounds;
        return {static_cast<float>(latest.left - previous.left), static_cast<float>(latest.top - previous.top)};
    }

    /**
     * @brief Gets the accumulated ROI (union of all ROIs in history) for a tracked object.
     * @param obj The tracked object.
     * @return The accumulated ROI bounding box, or empty rectangle if no history.
     */
    [[nodiscard]] sf::rectangle get_accumulated_roi(const detected_object &obj) {
        if (obj.history.empty()) {
            return sf::rectangle{0, 0, 0, 0};
        }
        sf::rectangle accumulated = obj.history[0].bounds;
        for (size_t i = 1; i < obj.history.size(); ++i) {
            accumulated.expand_with(obj.history[i].bounds);
        }
        return accumulated;
    }

    /**
     * @brief Union-Find helper: Find root with path compression (recursive).
     * @param parent Parent array for Union-Find.
     * @param x Element to find root for.
     * @return Root of x.
     */
    [[nodiscard]] size_t union_find_find(std::vector<size_t> &parent, const size_t x) {
        if (parent[x] != x) {
            parent[x] = union_find_find(parent, parent[x]); // Path compression
        }
        return parent[x];
    }

    /**
     * @brief Union-Find helper: Union two sets.
     * @param parent Parent array for Union-Find.
     * @param x First element.
     * @param y Second element.
     */
    void union_find_union(std::vector<size_t> &parent, const size_t x, const size_t y) {
        const size_t root_x = union_find_find(parent, x);
        const size_t root_y = union_find_find(parent, y);
        if (root_x != root_y) {
            parent[root_y] = root_x;
        }
    }

    /**
     * @brief Finds a tracked object by overlap using Union-Find recursive matching.
     * @param tracked_objects Vector of tracked objects.
     * @param detection_bbox Detection bounding box.
     * @param overlap_threshold IOU threshold for matching.
     * @return Iterator to matched object, or end() if no match.
     */
    [[nodiscard]] std::vector<detected_object>::iterator
    find_object_by_overlap(std::vector<detected_object> &tracked_objects, const sf::rectangle &detection_bbox,
                           const float overlap_threshold) {
        if (tracked_objects.empty()) {
            return tracked_objects.end();
        }

        const size_t num_detections = 1;
        const size_t num_tracked = tracked_objects.size();
        const size_t total_nodes = num_detections + num_tracked;

        // Initialize Union-Find: detection at index 0, tracked objects at indices 1..num_tracked
        std::vector<size_t> parent(total_nodes);
        for (size_t i = 0; i < total_nodes; ++i) {
            parent[i] = i;
        }

        // Build overlap graph: detection to tracked objects
        for (size_t j = 0; j < num_tracked; ++j) {
            const auto last_roi = get_last_roi(tracked_objects[j]);
            if (last_roi.is_valid()) {
                const double iou = detection_bbox.calculate_iou(last_roi);
                if (iou >= static_cast<double>(overlap_threshold)) {
                    union_find_union(parent, 0, 1 + j); // Detection (0) matches tracked object (1+j)
                }
            }
        }

        // Build overlap graph: tracked objects to tracked objects (transitive overlaps)
        for (size_t i = 0; i < num_tracked; ++i) {
            const auto accumulated_i = get_accumulated_roi(tracked_objects[i]);
            if (!accumulated_i.is_valid()) {
                continue;
            }
            for (size_t j = i + 1; j < num_tracked; ++j) {
                const auto accumulated_j = get_accumulated_roi(tracked_objects[j]);
                if (accumulated_j.is_valid()) {
                    const double iou = accumulated_i.calculate_iou(accumulated_j);
                    if (iou >= static_cast<double>(overlap_threshold)) {
                        union_find_union(parent, 1 + i, 1 + j); // Tracked object i overlaps tracked object j
                    }
                }
            }
        }

        // Find root of detection
        const size_t detection_root = union_find_find(parent, 0);

        // Find tracked object with same root (highest IOU if multiple)
        std::vector<detected_object>::iterator best_match = tracked_objects.end();
        double best_iou = 0.0;

        for (size_t j = 0; j < num_tracked; ++j) {
            const size_t tracked_root = union_find_find(parent, 1 + j);
            if (tracked_root == detection_root) {
                const auto last_roi = get_last_roi(tracked_objects[j]);
                if (last_roi.is_valid()) {
                    const double iou = detection_bbox.calculate_iou(last_roi);
                    if (iou > best_iou) {
                        best_iou = iou;
                        best_match = tracked_objects.begin() + static_cast<ptrdiff_t>(j);
                    }
                }
            }
        }

        return best_match;
    }

    /**
     * @brief Merges tracked objects that are in the same Union-Find component.
     * @param tracked_objects Vector of tracked objects (will be modified).
     * @param overlap_threshold IOU threshold for merging.
     */
    void merge_tracked_objects_in_components(std::vector<detected_object> &tracked_objects,
                                             const float overlap_threshold) {
        if (tracked_objects.size() <= 1) {
            return;
        }

        const size_t num_tracked = tracked_objects.size();
        std::vector<size_t> parent(num_tracked);
        for (size_t i = 0; i < num_tracked; ++i) {
            parent[i] = i;
        }

        // Build overlap graph: tracked objects to tracked objects
        for (size_t i = 0; i < num_tracked; ++i) {
            const auto accumulated_i = get_accumulated_roi(tracked_objects[i]);
            if (!accumulated_i.is_valid()) {
                continue;
            }
            for (size_t j = i + 1; j < num_tracked; ++j) {
                const auto accumulated_j = get_accumulated_roi(tracked_objects[j]);
                if (accumulated_j.is_valid()) {
                    const double iou = accumulated_i.calculate_iou(accumulated_j);
                    if (iou >= static_cast<double>(overlap_threshold)) {
                        union_find_union(parent, i, j);
                    }
                }
            }
        }

        // Group tracked objects by root
        std::unordered_map<size_t, std::vector<size_t>> groups;
        for (size_t i = 0; i < num_tracked; ++i) {
            const size_t root = union_find_find(parent, i);
            groups[root].push_back(i);
        }

        // Merge objects in same group (keep first, merge history into it)
        std::vector<bool> to_remove(num_tracked, false);
        for (const auto &[root, indices]: groups) {
            if (indices.size() <= 1) {
                continue; // No merging needed
            }

            // Keep first object, merge others into it
            const size_t keep_idx = indices[0];
            auto &keep_obj = tracked_objects[keep_idx];

            for (size_t idx = 1; idx < indices.size(); ++idx) {
                const size_t merge_idx = indices[idx];
                auto &merge_obj = tracked_objects[merge_idx];

                // Merge history (append all entries from merge_obj)
                keep_obj.history.insert(keep_obj.history.end(), merge_obj.history.begin(), merge_obj.history.end());

                // Update state: if merge_obj was detected, keep detected state
                if (merge_obj.state == object_state::detected) {
                    keep_obj.state = object_state::detected;
                }

                // Update missing count: use minimum
                keep_obj.missing_frames_count = std::min(keep_obj.missing_frames_count, merge_obj.missing_frames_count);

                // Mark for removal
                to_remove[merge_idx] = true;
            }

            // Trim merged history
            trim_history_if_needed(keep_obj);
        }

        // Remove merged objects (in reverse order to preserve indices)
        for (size_t i = num_tracked; i > 0; --i) {
            const size_t idx = i - 1;
            if (to_remove[idx]) {
                tracked_objects.erase(tracked_objects.begin() + static_cast<ptrdiff_t>(idx));
            }
        }
    }
} // namespace

std::vector<detected_object>::iterator find_object_by_id(std::vector<detected_object> &tracked_objects,
                                                         const uint32_t object_id) {
    return std::ranges::find_if(tracked_objects,
                                [object_id](const detected_object &obj) { return obj.object_id == object_id; });
}

void update_existing_objects(std::vector<detected_object> &tracked_objects,
                             const std::vector<detection_result> &detections, const uint32_t frame_number,
                             const float min_confidence, const roi_accumulation_mode accumulation_mode,
                             const float overlap_threshold, const stabilization_config &stabilization) {
    for (const auto &detection: detections) {
        std::vector<detected_object>::iterator obj_iter;

        if (accumulation_mode == roi_accumulation_mode::object_id) {
            // Use object_id-based matching (current behavior)
            obj_iter = find_object_by_id(tracked_objects, static_cast<uint32_t>(detection.object_id));
        } else {
            // Use overlap-based matching (recursive Union-Find)
            obj_iter = find_object_by_overlap(tracked_objects, detection.bounding_box, overlap_threshold);
        }

        if (obj_iter != tracked_objects.end()) {
            // Object exists, update it
            const uint32_t prev_missing_count = obj_iter->missing_frames_count;
            obj_iter->missing_frames_count = 0;
            const auto [velocity_x, velocity_y] = estimate_recent_velocity(*obj_iter);
            const auto stabilized_bounds = stabilize_bounds(detection.bounding_box, obj_iter->stable_state,
                                                            stabilization, velocity_x, velocity_y);

            if (detection.confidence >= min_confidence) {
                // Happy case: sufficient confidence
                // Trim history before adding new entry to prevent large reallocations
                trim_history_if_needed(*obj_iter);
                obj_iter->state = object_state::detected;
                obj_iter->history.emplace_back(stabilized_bounds, detection.confidence, frame_number,
                                               sf::yuv_color::blue);
                if (prev_missing_count > 0) {
                    sf::info(roi_detection_processing,
                             "[Frame %u] Object %u RE-DETECTED after %u missing frames - changing from MISSING to "
                             "DETECTED",
                             frame_number, obj_iter->object_id, prev_missing_count);
                } else {
                    sf::debug(roi_detection_processing,
                              "[Frame %u] Object %u updated: DETECTED (conf=%.2f, bbox=(%d,%d,%d,%d))", frame_number,
                              obj_iter->object_id, static_cast<double>(detection.confidence), stabilized_bounds.left,
                              stabilized_bounds.top, stabilized_bounds.width, stabilized_bounds.height);
                }
            } else {
                // Low confidence case
                // Trim history before adding new entry to prevent large reallocations
                trim_history_if_needed(*obj_iter);
                obj_iter->state = object_state::detected_no_confidence;
                obj_iter->history.emplace_back(stabilized_bounds, detection.confidence, frame_number,
                                               sf::yuv_color::purple);
                sf::debug(roi_detection_processing,
                          "[Frame %u] Object %u updated: LOW_CONF (conf=%.2f, bbox=(%d,%d,%d,%d))", frame_number,
                          obj_iter->object_id, static_cast<double>(detection.confidence), stabilized_bounds.left,
                          stabilized_bounds.top, stabilized_bounds.width, stabilized_bounds.height);
            }
        }
    }
}

void create_new_objects(std::vector<detected_object> &tracked_objects, const std::vector<detection_result> &detections,
                        const uint32_t frame_number, const float min_confidence,
                        const roi_accumulation_mode accumulation_mode, const float overlap_threshold) {
    // First, merge tracked objects in same component if using overlap mode
    if (accumulation_mode == roi_accumulation_mode::overlap) {
        merge_tracked_objects_in_components(tracked_objects, overlap_threshold);
    }

    for (const auto &detection: detections) {
        std::vector<detected_object>::iterator obj_iter;

        if (accumulation_mode == roi_accumulation_mode::object_id) {
            // Use object_id-based matching (current behavior)
            obj_iter = find_object_by_id(tracked_objects, static_cast<uint32_t>(detection.object_id));
        } else {
            // Use overlap-based matching (recursive Union-Find)
            obj_iter = find_object_by_overlap(tracked_objects, detection.bounding_box, overlap_threshold);
        }

        if (obj_iter == tracked_objects.end()) {
            // New object, create it
            // For overlap mode, generate a unique ID (use next available ID)
            uint32_t new_object_id;
            if (accumulation_mode == roi_accumulation_mode::object_id) {
                new_object_id = static_cast<uint32_t>(detection.object_id);
            } else {
                // Generate unique ID: find max existing ID + 1
                uint32_t max_id = 0;
                for (const auto &obj: tracked_objects) {
                    max_id = std::max(max_id, obj.object_id);
                }
                new_object_id = max_id + 1;
            }
            detected_object new_obj(new_object_id);
            new_obj.stable_state.smoothed_x = static_cast<float>(detection.bounding_box.left);
            new_obj.stable_state.smoothed_y = static_cast<float>(detection.bounding_box.top);
            new_obj.stable_state.smoothed_w = static_cast<float>(detection.bounding_box.width);
            new_obj.stable_state.smoothed_h = static_cast<float>(detection.bounding_box.height);
            new_obj.stable_state.initialized = true;

            if (detection.confidence >= min_confidence) {
                // Happy case: sufficient confidence
                new_obj.state = object_state::detected;
                new_obj.history.emplace_back(detection.bounding_box, detection.confidence, frame_number,
                                             sf::yuv_color::blue);
            } else {
                // Low confidence case
                new_obj.state = object_state::detected_no_confidence;
                new_obj.history.emplace_back(detection.bounding_box, detection.confidence, frame_number,
                                             sf::yuv_color::purple);
            }
            // No need to trim for new objects (only 1 entry), but good practice
            trim_history_if_needed(new_obj);

            tracked_objects.push_back(std::move(new_obj));
        }
    }
}

void handle_missing_objects(std::vector<detected_object> &tracked_objects, const uint32_t frame_number,
                            const int frame_width, const int frame_height, const uint32_t max_missing_frames,
                            const bool enable_prediction) {
    for (auto &obj: tracked_objects) {
        // Check if object was updated in this frame
        if (obj.history.empty() || obj.history.back().frame_number < frame_number) {
            obj.missing_frames_count++;

            if (obj.missing_frames_count >= max_missing_frames) {
                obj.state = object_state::expired;
                sf::info(roi_detection_processing,
                         "[Frame %u] Object %u EXPIRED after %u missing frames (max=%u) - removing from tracking",
                         frame_number, obj.object_id, obj.missing_frames_count, max_missing_frames);
            } else {
                obj.state = object_state::missing;
                sf::info(roi_detection_processing, "[Frame %u] Object %u MISSING (count=%u/%u) - last seen at frame %u",
                         frame_number, obj.object_id, obj.missing_frames_count, max_missing_frames,
                         obj.history.empty() ? 0U : obj.history.back().frame_number);
                if (enable_prediction) {
                    // Prediction is enabled - predict the object's position
                    sf::info(roi_detection_processing,
                             "[Frame %u] Object %u: PREDICTION ENABLED - calling predict_object_position", frame_number,
                             obj.object_id);
                    predict_object_position(obj, frame_number, frame_width, frame_height);
                } else {
                    sf::debug(roi_detection_processing,
                              "[Frame %u] Object %u: PREDICTION DISABLED - object remains in MISSING state without "
                              "prediction",
                              frame_number, obj.object_id);
                }
                // If prediction is disabled, the object remains in 'missing' state without prediction
            }
        }
    }
}
