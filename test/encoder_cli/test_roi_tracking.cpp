// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "../../encoder_cli/src/processing/roi_detection_processing.h"
#include "../../encoder_cli/src/roi/roi_manager.h"
#include "../../encoder_cli/src/roi/roi_manager/tracking.h"

namespace {
    [[nodiscard]] detected_object make_tracked_object(const uint32_t object_id, const sf::rectangle &bounds,
                                                      const uint32_t frame_number) {
        detected_object object(object_id);
        object.history.emplace_back(bounds, 0.9f, frame_number, sf::yuv_color::blue);
        object.stable_state.smoothed_x = static_cast<float>(bounds.left);
        object.stable_state.smoothed_y = static_cast<float>(bounds.top);
        object.stable_state.smoothed_w = static_cast<float>(bounds.width);
        object.stable_state.smoothed_h = static_cast<float>(bounds.height);
        object.stable_state.initialized = true;
        return object;
    }
} // namespace

TEST(RoiTrackingHelpersTest, create_new_objects_overlap_mode_assigns_incremental_id) {
    std::vector<detected_object> tracked_objects;
    tracked_objects.push_back(make_tracked_object(3, sf::rectangle{0, 0, 10, 10}, 1));

    const std::vector<detection_result> detections = {
            detection_result(sf::rectangle{300, 300, 12, 12}, 0, 0.95f, 99),
    };

    create_new_objects(tracked_objects, detections, 2, 0.5f, roi_accumulation_mode::overlap, 0.3f);

    ASSERT_EQ(tracked_objects.size(), 2U);
    const auto new_object_iter = std::find_if(tracked_objects.begin(), tracked_objects.end(),
                                              [](const detected_object &object) { return object.object_id == 4U; });
    ASSERT_NE(new_object_iter, tracked_objects.end());
    EXPECT_EQ(new_object_iter->state, object_state::detected);
    ASSERT_FALSE(new_object_iter->history.empty());
    EXPECT_EQ(new_object_iter->history.back().frame_number, 2U);
}

TEST(RoiTrackingHelpersTest, update_existing_objects_overlap_mode_uses_transitive_component_matching) {
    std::vector<detected_object> tracked_objects;
    tracked_objects.push_back(make_tracked_object(1, sf::rectangle{0, 0, 10, 10}, 1));
    tracked_objects.push_back(make_tracked_object(2, sf::rectangle{5, 0, 10, 10}, 1));

    const std::vector<detection_result> detections = {
            detection_result(sf::rectangle{6, 0, 10, 10}, 0, 0.9f, 500),
    };
    stabilization_config stabilization{};
    stabilization.enabled = false;

    update_existing_objects(tracked_objects, detections, 2, 0.5f, roi_accumulation_mode::overlap, 0.3f, stabilization);

    ASSERT_EQ(tracked_objects.size(), 2U);
    EXPECT_EQ(tracked_objects[0].history.size(), 1U);
    EXPECT_EQ(tracked_objects[1].history.size(), 2U);
    EXPECT_EQ(tracked_objects[1].history.back().frame_number, 2U);
    EXPECT_EQ(tracked_objects[1].state, object_state::detected);
}

TEST(RoiTrackingHelpersTest, create_new_objects_overlap_mode_merges_existing_component_before_processing_detections) {
    std::vector<detected_object> tracked_objects;
    tracked_objects.push_back(make_tracked_object(10, sf::rectangle{100, 100, 20, 20}, 1));
    tracked_objects.push_back(make_tracked_object(11, sf::rectangle{105, 100, 20, 20}, 1));

    const std::vector<detection_result> detections;
    create_new_objects(tracked_objects, detections, 2, 0.5f, roi_accumulation_mode::overlap, 0.3f);

    EXPECT_EQ(tracked_objects.size(), 1U);
}

TEST(RoiTrackingHelpersTest, handle_missing_objects_transitions_to_missing_and_then_expired) {
    std::vector<detected_object> tracked_objects;
    tracked_objects.push_back(make_tracked_object(7, sf::rectangle{20, 20, 10, 10}, 1));

    handle_missing_objects(tracked_objects, 2, 640, 480, 2, false);
    ASSERT_EQ(tracked_objects.size(), 1U);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 1U);

    handle_missing_objects(tracked_objects, 3, 640, 480, 2, false);
    EXPECT_EQ(tracked_objects[0].state, object_state::expired);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 2U);
}

TEST(RoiTrackingHelpersTest, find_object_by_id_returns_end_for_unknown_id) {
    std::vector<detected_object> tracked_objects;
    tracked_objects.push_back(make_tracked_object(1, sf::rectangle{0, 0, 8, 8}, 1));

    const auto object_iter = find_object_by_id(tracked_objects, 999);
    EXPECT_EQ(object_iter, tracked_objects.end());
}
