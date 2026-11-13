// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <vector>

#include "../../encoder_cli/src/processing/roi_detection_processing.h"
#include "../../encoder_cli/src/roi/roi_manager.h"
#include "../../encoder_cli/src/roi/roi_tracker_view.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_roi_tracker_view.cpp
 * @brief Unit tests for the ROI tracker narrow view interface.
 *
 * Exercises the roi_tracker_view seam directly, without constructing a full
 * global_data object (per the PRD testing decision).
 */

class RoiTrackerViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        tracker = std::make_unique<roi_manager>(0.5f, 5, true, roi_accumulation_mode::object_id, 0.33f,
                                                stabilization_config{});
        view = std::make_unique<roi_tracker_view>(*tracker);
    }

    void TearDown() override {
        view.reset();
        tracker.reset();
    }

    std::unique_ptr<roi_manager> tracker;
    std::unique_ptr<roi_tracker_view> view;
};

TEST_F(RoiTrackerViewTest, ProcessFrameTracksObject) {
    std::vector<detection_result> detections;
    detections.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);

    const auto tracked = view->process_frame(detections, 1, 1920, 1080);

    ASSERT_EQ(tracked.size(), 1);
    EXPECT_EQ(tracked[0].object_id, 1);
    EXPECT_EQ(tracked[0].state, object_state::detected);
    EXPECT_EQ(tracked[0].history.size(), 1);
}

TEST_F(RoiTrackerViewTest, GetTrackedObjectsReflectsTrackerState) {
    std::vector<detection_result> detections;
    detections.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);

    (void) view->process_frame(detections, 1, 1920, 1080);

    // The view must observe the same tracker state the aggregate owns.
    const auto &tracked = view->get_tracked_objects();
    ASSERT_EQ(tracked.size(), 1);
    EXPECT_EQ(tracked[0].state, object_state::detected);
    EXPECT_EQ(tracker->get_tracked_objects().size(), 1);
}

TEST_F(RoiTrackerViewTest, AdvanceFrameWithoutDetectionMarksObjectMissing) {
    std::vector<detection_result> detections;
    detections.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) view->process_frame(detections, 1, 1920, 1080);

    view->advance_frame_without_detection(2, 1920, 1080);

    const auto &tracked = view->get_tracked_objects();
    ASSERT_EQ(tracked.size(), 1);
    EXPECT_EQ(tracked[0].state, object_state::missing);
    EXPECT_EQ(tracked[0].missing_frames_count, 1);
    EXPECT_EQ(tracked[0].history.back().frame_number, 2);
}
