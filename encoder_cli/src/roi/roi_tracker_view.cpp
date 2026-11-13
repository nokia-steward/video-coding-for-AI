// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "roi_tracker_view.h"

std::vector<detected_object> roi_tracker_view::process_frame(const std::vector<detection_result> &detections,
                                                             const uint32_t frame_number, const int frame_width,
                                                             const int frame_height) {
    return tracker.process_frame(detections, frame_number, frame_width, frame_height);
}

void roi_tracker_view::advance_frame_without_detection(const int frame_number, const int width, const int height) {
    tracker.advance_frame_without_detection(frame_number, width, height);
}

const std::vector<detected_object> &roi_tracker_view::get_tracked_objects() const {
    return tracker.get_tracked_objects();
}
