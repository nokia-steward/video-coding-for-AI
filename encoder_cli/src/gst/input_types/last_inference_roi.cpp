// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "last_inference_roi.h"

#include "../../../../libvcai/include/logging.h"

/**
 * @file last_inference_roi.cpp
 * @brief Implementation of last inference ROI data storage and access.
 */

/// GStreamer debug category for last inference ROI tracking.
GST_DEBUG_CATEGORY_STATIC(last_inference_roi_cat);

namespace {

    /**
     * @brief Initializes the debug category for last inference ROI data.
     */
    void init_debug_category() {
        static bool initialized = false;
        if (!initialized) {
            GST_DEBUG_CATEGORY_INIT(last_inference_roi_cat, "last-inference-roi", 0, "Last inference ROI data storage");
            initialized = true;
        }
    }

} // namespace

void last_inference_roi_data::update(std::vector<split_rectangle> new_split_rects, std::vector<sf::rectangle> new_rects,
                                     GstClockTime pts, guint64 frame_num) {
    init_debug_category();
    std::lock_guard lock(mutex);

    split_rectangles = std::move(new_split_rects);
    rectangles = std::move(new_rects);
    source_pts = pts;
    source_frame_number = frame_num;
    reuse_count = 0;
}

bool last_inference_roi_data::get_for_bypass(std::vector<split_rectangle> &out_split_rects,
                                             std::vector<sf::rectangle> &out_rects) {
    init_debug_category();
    std::lock_guard lock(mutex);

    if (source_pts == GST_CLOCK_TIME_NONE) {
        sf::warning(last_inference_roi_cat, "No inference ROI data available yet for bypass frame");
        return false;
    }

    out_split_rects = split_rectangles;
    out_rects = rectangles;
    reuse_count++;

    return true;
}

/**
 * @brief Checks whether inference ROI data is available.
 * @return True if valid data has been stored.
 */
bool last_inference_roi_data::has_data() const {
    std::lock_guard lock(mutex);
    return source_pts != GST_CLOCK_TIME_NONE;
}

void last_inference_roi_data::clear() {
    std::lock_guard lock(mutex);
    split_rectangles.clear();
    rectangles.clear();
    source_pts = GST_CLOCK_TIME_NONE;
    source_frame_number = 0;
    reuse_count = 0;
}
