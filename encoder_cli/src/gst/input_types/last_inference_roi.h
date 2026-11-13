// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include <mutex>
#include <vector>

#include "../../../libvcai/include/rectangle.h"
#include "../../roi/rectangle_splitter.h"

/**
 * @file last_inference_roi.height
 * @brief Declares structure for storing the most recent inference ROI data for bypass frames.
 */

/**
 * @brief Stores the most recent inference result for use by bypass frames.
 *
 * When an inference frame is processed, its ROI detection results are stored
 * here. Subsequent bypass frames copy this data instead of running inference.
 *
 * Thread-safety: All access must be protected by the mutex.
 */
struct last_inference_roi_data {
    /**
     * @brief Default constructor.
     */
    last_inference_roi_data() :
        split_rectangles(), rectangles(), source_pts(GST_CLOCK_TIME_NONE), source_frame_number(0), reuse_count(0),
        mutex() {}

    /**
     * @brief The split rectangles from the most recent inference frame.
     *
     * These include bounding boxes, object IDs, colors, and confidence scores.
     * Bypass frames will use this data directly for their ROI processing.
     */
    std::vector<split_rectangle> split_rectangles;

    /**
     * @brief Plain rectangles extracted from split_rectangles.
     *
     * Pre-computed for efficiency when only bounds are needed.
     */
    std::vector<sf::rectangle> rectangles;

    /**
     * @brief PTS of the inference frame that provided this data.
     *
     * Used for debugging and to verify data freshness.
     */
    GstClockTime source_pts;

    /**
     * @brief Frame number of the inference frame that provided this data.
     */
    guint64 source_frame_number;

    /**
     * @brief Number of times this data has been reused by bypass frames.
     *
     * Reset to 0 when new inference data arrives.
     */
    guint64 reuse_count;

    /**
     * @brief Protects all members from concurrent access.
     */
    mutable std::mutex mutex;

    /**
     * @brief Updates the stored ROI data from an inference frame result.
     *
     * @param new_split_rects The split rectangles from inference detection.
     * @param new_rects The plain rectangles from inference detection.
     * @param pts The PTS of the inference frame.
     * @param frame_num The frame number of the inference frame.
     */
    void update(std::vector<split_rectangle> new_split_rects, std::vector<sf::rectangle> new_rects, GstClockTime pts,
                guint64 frame_num);

    /**
     * @brief Gets a copy of the stored ROI data for a bypass frame.
     *
     * Increments the reuse counter and returns copies of the stored data.
     *
     * @param out_split_rects Output: copy of split rectangles.
     * @param out_rects Output: copy of plain rectangles.
     * @return True if valid data was available, false if no inference has occurred yet.
     */
    [[nodiscard]] bool get_for_bypass(std::vector<split_rectangle> &out_split_rects,
                                      std::vector<sf::rectangle> &out_rects);

    /**
     * @brief Checks if valid ROI data is available.
     *
     * @return True if at least one inference frame has been processed.
     */
    [[nodiscard]] bool has_data() const;

    /**
     * @brief Clears all stored data.
     *
     * Called during shutdown or pipeline reset.
     */
    void clear();
};
