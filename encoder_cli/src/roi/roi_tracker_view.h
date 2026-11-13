// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <vector>

#include "roi_manager.h"

/**
 * @file roi_tracker_view.height
 * @brief Non-owning view over the ROI tracker, exposing only the tracking slice.
 *
 * The ROI tracker (`roi_manager`) is a field of the aggregate. This view presents
 * that tracker through a small, typed surface so the ROI strategy and processing
 * layers can drive tracking without reaching into the whole `global_data` object.
 * It is non-owning: the aggregate still owns the tracker.
 */

/**
 * @brief A non-owning view over the ROI tracker.
 *
 * Exposes exactly the tracker operations the ROI consumers use: feeding detections
 * through `process_frame`, advancing tracking on bypass frames, and reading back the
 * tracked objects. The underlying `roi_manager` is owned by the aggregate and must
 * outlive the view.
 */
struct roi_tracker_view {
    /**
     * @brief Construct a view over an existing ROI tracker.
     * @param manager The tracker owned by the aggregate. Must outlive the view.
     */
    explicit roi_tracker_view(roi_manager &manager) : tracker(manager) {}

    roi_tracker_view(const roi_tracker_view &) = delete;
    roi_tracker_view &operator=(const roi_tracker_view &) = delete;

    /**
     * @brief Feed a frame with new detections and update object tracking.
     * @param detections Vector of new detections from the inference engine.
     * @param frame_number Current frame number.
     * @param frame_width Width of the frame for boundary checking.
     * @param frame_height Height of the frame for boundary checking.
     * @return Vector of all currently tracked objects.
     */
    [[nodiscard]] std::vector<detected_object> process_frame(const std::vector<detection_result> &detections,
                                                             uint32_t frame_number, int frame_width, int frame_height);

    /**
     * @brief Advance the frame counter without new detections.
     * @param frame_number The current frame number.
     * @param width Frame width for boundary checking.
     * @param height Frame height for boundary checking.
     */
    void advance_frame_without_detection(int frame_number, int width, int height);

    /**
     * @brief Get all currently tracked objects.
     * @return Vector of all tracked objects.
     */
    [[nodiscard]] const std::vector<detected_object> &get_tracked_objects() const;

private:
    roi_manager &tracker;
};
