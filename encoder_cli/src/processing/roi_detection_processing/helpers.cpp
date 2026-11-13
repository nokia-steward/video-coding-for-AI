// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "helpers.h"
#include "../../../../libvcai/include/logging.h"
#include "../../roi/rectangle_splitter.h"
#include "../../roi/roi_manager.h"
#include "../frame_processing_utils.h"
#include "../roi_detection_processing.h"

#include <algorithm>
#include <gst/gst.h>
#include <string>
#include <utility>
#include <vector>

/**
 * @file helpers.cpp
 * @brief Implementation of helper functions for ROI detection processing.
 */

GST_DEBUG_CATEGORY_EXTERN(roi_detection_processing);

std::vector<split_rectangle>
extract_split_rectangles_from_tracked_objects(const std::vector<detected_object> &tracked_objects,
                                              const frame_properties &props, const int frame_number) {
    // Use the rectangle splitting algorithm to handle overlaps
    return split_overlapping_rectangles(tracked_objects, props.width, props.height, frame_number);
}

/**
 * @brief Logs basic detection counts for a frame.
 * @param input_rectangles Detected rectangles.
 * @param frame_number Frame index for logging context.
 */
void log_detection_debug_info(const std::vector<sf::rectangle> &input_rectangles, const int frame_number) {
    sf::debug(roi_detection_processing, "Frame %d: %zu ROIs detected", frame_number, input_rectangles.size());
}

/**
 * @brief Logs tracking details for detected objects.
 * @param tracked_objects Tracked object list.
 * @param frame_number Frame index for logging context.
 */
void log_tracking_info(const std::vector<detected_object> &tracked_objects, const int frame_number) {
    if (tracked_objects.empty()) {
        return;
    }

    sf::debug(roi_detection_processing, "Frame %d: Tracking %zu objects", frame_number, tracked_objects.size());

    for (const auto &obj: tracked_objects) {
        if (!obj.history.empty()) {
            const auto &latest_data = obj.history.back();
            const char *state_str = obj.state == object_state::detected                 ? "DETECTED"
                                    : obj.state == object_state::detected_no_confidence ? "LOW_CONF"
                                    : obj.state == object_state::missing                ? "MISSING"
                                                                                        : "EXPIRED";
            sf::debug(roi_detection_processing, "  Object %u: %s, BBox=(%d,%d,%d,%d), Conf=%.2f", obj.object_id,
                      state_str, latest_data.bounds.left, latest_data.bounds.top, latest_data.bounds.width,
                      latest_data.bounds.height, static_cast<double>(latest_data.confidence));
        }
    }
}

sf::rectangle inflate_and_clamp_roi(const sf::rectangle &roi, const int margin_px, const int frame_width,
                                    const int frame_height) {
    if (roi.width <= 0 || roi.height <= 0 || frame_width <= 0 || frame_height <= 0) {
        return sf::rectangle(0, 0, 0, 0);
    }

    const int safe_margin = std::max(0, margin_px);
    const int x1 = std::max(0, roi.left - safe_margin);
    const int y1 = std::max(0, roi.top - safe_margin);
    const int x2 = std::min(frame_width, roi.left + roi.width + safe_margin);
    const int y2 = std::min(frame_height, roi.top + roi.height + safe_margin);
    const int width = std::max(0, x2 - x1);
    const int height = std::max(0, y2 - y1);
    return sf::rectangle(x1, y1, width, height);
}

std::vector<sf::rectangle> apply_fixed_roi_margin(const std::vector<sf::rectangle> &input_rectangles,
                                                  const int margin_px, const int frame_width, const int frame_height) {
    std::vector<sf::rectangle> expanded_rectangles;
    expanded_rectangles.reserve(input_rectangles.size());
    for (const auto &rect: input_rectangles) {
        const sf::rectangle expanded = inflate_and_clamp_roi(rect, margin_px, frame_width, frame_height);
        expanded_rectangles.push_back(expanded.is_valid() ? expanded : rect);
    }
    return expanded_rectangles;
}

std::vector<std::pair<sf::rectangle, std::string>>
apply_fixed_roi_margin_with_ids(const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                                const int margin_px, const int frame_width, const int frame_height) {
    std::vector<std::pair<sf::rectangle, std::string>> expanded_rectangles_with_ids;
    expanded_rectangles_with_ids.reserve(rectangles_with_ids.size());
    for (const auto &[rect, id]: rectangles_with_ids) {
        const sf::rectangle expanded = inflate_and_clamp_roi(rect, margin_px, frame_width, frame_height);
        expanded_rectangles_with_ids.emplace_back(expanded.is_valid() ? expanded : rect, id);
    }
    return expanded_rectangles_with_ids;
}

std::vector<std::pair<sf::rectangle, std::string>>
prepare_rectangles_with_ids_for_random_access(const std::vector<sf::rectangle> &input_rectangles,
                                              const std::vector<split_rectangle> &split_rectangles,
                                              const bool is_static) {
    std::vector<std::pair<sf::rectangle, std::string>> rectangles_with_ids;
    rectangles_with_ids.reserve(input_rectangles.size());

    if (is_static) {
        // Static ROIs: use index as object ID
        for (size_t i = 0; i < input_rectangles.size(); ++i) {
            rectangles_with_ids.emplace_back(input_rectangles[i], std::to_string(i));
        }
    } else {
        // Inference mode: use original_object_id from split_rectangles
        if (!split_rectangles.empty() && split_rectangles.size() == input_rectangles.size()) {
            for (size_t i = 0; i < input_rectangles.size(); ++i) {
                rectangles_with_ids.emplace_back(input_rectangles[i],
                                                 std::to_string(split_rectangles[i].original_object_id));
            }
        } else {
            // Fallback: match by bounds or use index
            for (size_t i = 0; i < input_rectangles.size(); ++i) {
                bool found = false;
                for (const auto &split_rect: split_rectangles) {
                    if (split_rect.bounds.left == input_rectangles[i].left &&
                        split_rect.bounds.top == input_rectangles[i].top &&
                        split_rect.bounds.width == input_rectangles[i].width &&
                        split_rect.bounds.height == input_rectangles[i].height) {
                        rectangles_with_ids.emplace_back(input_rectangles[i],
                                                         std::to_string(split_rect.original_object_id));
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    rectangles_with_ids.emplace_back(input_rectangles[i], "unknown_" + std::to_string(i));
                }
            }
        }
    }

    return rectangles_with_ids;
}
