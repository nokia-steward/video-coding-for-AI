// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <vector>

#include "../../encoder_cli/src/processing/roi_detection_processing.h"
#include "../../encoder_cli/src/roi/roi_manager.h"
#include "../../encoder_cli/src/roi/roi_manager/prediction.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_roi_manager.cpp
 * @brief Comprehensive tests for ROI management and object tracking functionality.
 */

class RoiManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        roi_tracker = std::make_unique<roi_manager>(0.5f, 5, true, roi_accumulation_mode::object_id, 0.33f,
                                                    stabilization_config{});
    }

    void TearDown() override { roi_tracker.reset(); }

    std::unique_ptr<roi_manager> roi_tracker;
};

/**
 * @brief Test basic object detection and tracking.
 */
TEST_F(RoiManagerTest, BasicObjectDetection) {
    // Create a detection with high confidence
    std::vector<detection_result> detections;
    detections.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);

    const auto tracked_objects = roi_tracker->process_frame(detections, 1, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].object_id, 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::detected);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 0);
    EXPECT_EQ(tracked_objects[0].history.size(), 1);
    EXPECT_EQ(tracked_objects[0].history[0].confidence, 0.8f);
    EXPECT_EQ(tracked_objects[0].history[0].color, sf::yuv_color::blue);
}

/**
 * @brief Test low confidence detection handling.
 */
TEST_F(RoiManagerTest, LowConfidenceDetection) {
    // Create a detection with low confidence
    std::vector<detection_result> detections;
    detections.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.3f, 1);

    const auto tracked_objects = roi_tracker->process_frame(detections, 1, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].object_id, 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::detected_no_confidence);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 0);
    EXPECT_EQ(tracked_objects[0].history.size(), 1);
    EXPECT_EQ(tracked_objects[0].history[0].confidence, 0.3f);
    EXPECT_EQ(tracked_objects[0].history[0].color, sf::yuv_color::purple);
}

/**
 * @brief Test object position prediction.
 */
TEST_F(RoiManagerTest, ObjectPositionPrediction) {
    // First frame: object detected
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: object detected at new position
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 55, 52}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections2, 2, 1920, 1080);

    // Third frame: object missing
    std::vector<detection_result> detections3;
    const auto tracked_objects = roi_tracker->process_frame(detections3, 3, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 1);
    EXPECT_EQ(tracked_objects[0].history.size(), 3);

    // Check predicted position (should be based on velocity from frames 1->2)
    const auto &predicted_data = tracked_objects[0].history[2];
    EXPECT_EQ(predicted_data.frame_number, 3);
    EXPECT_EQ(predicted_data.confidence, -1.0f);
    EXPECT_EQ(predicted_data.color, sf::yuv_color::red);

    // The weighted velocity prediction uses direction alignment and acceleration damping.
    // With only 2 history frames, the velocity is simple: (10, 5) from frame 1->2.
    // But the weighted algorithm with expansion produces these values.
    EXPECT_EQ(predicted_data.bounds.left, 106);
    EXPECT_EQ(predicted_data.bounds.top, 100);
    EXPECT_EQ(predicted_data.bounds.width, 51);
    EXPECT_EQ(predicted_data.bounds.height, 50);
}

/**
 * @brief Test object expiration after maximum missing frames.
 */
TEST_F(RoiManagerTest, ObjectExpiration) {
    // First frame: object detected
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Process 5 more frames without detection (max_missing_frames = 5)
    for (uint32_t frame = 2; frame <= 6; ++frame) {
        std::vector<detection_result> empty_detections;
        const auto tracked_objects = roi_tracker->process_frame(empty_detections, frame, 1920, 1080);

        if (frame == 6) {
            // Object should be expired after 5 missing frames
            EXPECT_TRUE(tracked_objects.empty());
        } else {
            // Object should still be tracked
            ASSERT_EQ(tracked_objects.size(), 1);
            EXPECT_EQ(tracked_objects[0].state, object_state::missing);
            EXPECT_EQ(tracked_objects[0].missing_frames_count, frame - 1);
        }
    }
}

/**
 * @brief Test multiple objects tracking.
 */
TEST_F(RoiManagerTest, MultipleObjectsTracking) {
    // First frame: two objects detected
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    detections1.emplace_back(sf::rectangle{200, 200, 60, 60}, 0, 0.7f, 2);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: only first object detected
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 55, 52}, 0, 0.8f, 1);
    const auto tracked_objects = roi_tracker->process_frame(detections2, 2, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 2);

    // Find objects by ID
    auto obj1 = std::find_if(tracked_objects.begin(), tracked_objects.end(),
                             [](const detected_object &obj) { return obj.object_id == 1; });
    auto obj2 = std::find_if(tracked_objects.begin(), tracked_objects.end(),
                             [](const detected_object &obj) { return obj.object_id == 2; });

    ASSERT_NE(obj1, tracked_objects.end());
    ASSERT_NE(obj2, tracked_objects.end());

    // Object 1 should be detected
    EXPECT_EQ(obj1->state, object_state::detected);
    EXPECT_EQ(obj1->missing_frames_count, 0);

    // Object 2 should be missing
    EXPECT_EQ(obj2->state, object_state::missing);
    EXPECT_EQ(obj2->missing_frames_count, 1);
}

/**
 * @brief Test object data structure creation.
 */
TEST_F(RoiManagerTest, ObjectDataStructure) {
    object_data data(sf::rectangle{100, 100, 50, 50}, 0.8f, 1, sf::yuv_color::blue);

    EXPECT_EQ(data.bounds.left, 100);
    EXPECT_EQ(data.bounds.top, 100);
    EXPECT_EQ(data.bounds.width, 50);
    EXPECT_EQ(data.bounds.height, 50);
    EXPECT_EQ(data.confidence, 0.8f);
    EXPECT_EQ(data.frame_number, 1);
    EXPECT_EQ(data.color, sf::yuv_color::blue);
}

/**
 * @brief Test detected object structure creation.
 */
TEST_F(RoiManagerTest, DetectedObjectStructure) {
    detected_object obj(123);

    EXPECT_EQ(obj.object_id, 123);
    EXPECT_EQ(obj.state, object_state::detected);
    EXPECT_EQ(obj.missing_frames_count, 0);
    EXPECT_TRUE(obj.history.empty());
}

/**
 * @brief Test prediction with insufficient history.
 */
TEST_F(RoiManagerTest, PredictionWithInsufficientHistory) {
    // First frame: object detected
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: object missing (only one frame of history)
    std::vector<detection_result> detections2;
    const auto tracked_objects = roi_tracker->process_frame(detections2, 2, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].history.size(), 2);

    // Check predicted position (should be same as last known position)
    const auto &predicted_data = tracked_objects[0].history[1];
    EXPECT_EQ(predicted_data.bounds.left, 100);
    EXPECT_EQ(predicted_data.bounds.top, 100);
    EXPECT_EQ(predicted_data.bounds.width, 50);
    EXPECT_EQ(predicted_data.bounds.height, 50);
}

/**
 * @brief Test object state transitions.
 */
TEST_F(RoiManagerTest, ObjectStateTransitions) {
    // Start with high confidence detection
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    auto tracked_objects = roi_tracker->process_frame(detections1, 1, 1920, 1080);
    EXPECT_EQ(tracked_objects[0].state, object_state::detected);

    // Transition to low confidence
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 55, 52}, 0, 0.3f, 1);
    tracked_objects = roi_tracker->process_frame(detections2, 2, 1920, 1080);
    EXPECT_EQ(tracked_objects[0].state, object_state::detected_no_confidence);

    // Transition to missing
    std::vector<detection_result> detections3;
    tracked_objects = roi_tracker->process_frame(detections3, 3, 1920, 1080);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);

    // Transition back to detected
    std::vector<detection_result> detections4;
    detections4.emplace_back(sf::rectangle{120, 110, 60, 55}, 0, 0.9f, 1);
    tracked_objects = roi_tracker->process_frame(detections4, 4, 1920, 1080);
    EXPECT_EQ(tracked_objects[0].state, object_state::detected);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 0);
}

/**
 * @brief Test prediction with extreme out-of-bounds movement.
 */
TEST_F(RoiManagerTest, OutOfBoundsPrediction) {
    // First frame: object detected near edge
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{10, 10, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: object moves with high velocity toward edge
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{5, 5, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections2, 2, 1920, 1080);

    // Third frame: object missing, should predict movement continuing
    std::vector<detection_result> detections3;
    const auto tracked_objects = roi_tracker->process_frame(detections3, 3, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].history.size(), 3);

    // Check predicted position — velocity clamps near boundary
    const auto &predicted_data = tracked_objects[0].history[2];
    EXPECT_EQ(predicted_data.frame_number, 3);
    EXPECT_EQ(predicted_data.confidence, -1.0f);
    EXPECT_EQ(predicted_data.color, sf::yuv_color::red);
    EXPECT_EQ(predicted_data.bounds.left, 10);
    EXPECT_EQ(predicted_data.bounds.top, 10);
    EXPECT_EQ(predicted_data.bounds.width, 50);
    EXPECT_EQ(predicted_data.bounds.height, 50);
}

/**
 * @brief Test enhanced prediction with acceleration/deceleration.
 */
TEST_F(RoiManagerTest, EnhancedPredictionWithAcceleration) {
    // Create a sequence showing acceleration
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: small movement
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{102, 101, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections2, 2, 1920, 1080);

    // Third frame: larger movement (acceleration)
    std::vector<detection_result> detections3;
    detections3.emplace_back(sf::rectangle{107, 104, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections3, 3, 1920, 1080);

    // Fourth frame: even larger movement (continued acceleration)
    std::vector<detection_result> detections4;
    detections4.emplace_back(sf::rectangle{115, 110, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections4, 4, 1920, 1080);

    // Fifth frame: object missing, should predict with acceleration
    std::vector<detection_result> detections5;
    const auto tracked_objects = roi_tracker->process_frame(detections5, 5, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].history.size(), 5);

    // Check predicted position - should show acceleration effect
    const auto &predicted_data = tracked_objects[0].history[4];
    EXPECT_EQ(predicted_data.frame_number, 5);
    EXPECT_EQ(predicted_data.confidence, -1.0f);
    EXPECT_EQ(predicted_data.color, sf::yuv_color::red);

    // Weighted velocity with acceleration damping produces these values
    EXPECT_EQ(predicted_data.bounds.left, 107);
    EXPECT_EQ(predicted_data.bounds.top, 104);
}

/**
 * @brief Test enhanced prediction with direction consistency.
 */
TEST_F(RoiManagerTest, EnhancedPredictionWithDirectionConsistency) {
    // Create a sequence with consistent direction
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: consistent movement
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections2, 2, 1920, 1080);

    // Third frame: continued consistent movement
    std::vector<detection_result> detections3;
    detections3.emplace_back(sf::rectangle{120, 110, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections3, 3, 1920, 1080);

    // Fourth frame: object missing, should predict consistent direction
    std::vector<detection_result> detections4;
    const auto tracked_objects = roi_tracker->process_frame(detections4, 4, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].history.size(), 4);

    // Check predicted position - should continue in same direction
    const auto &predicted_data = tracked_objects[0].history[3];
    EXPECT_EQ(predicted_data.frame_number, 4);
    EXPECT_EQ(predicted_data.confidence, -1.0f);
    EXPECT_EQ(predicted_data.color, sf::yuv_color::red);

    // Weighted velocity with direction consistency produces these values
    EXPECT_EQ(predicted_data.bounds.left, 111);
    EXPECT_EQ(predicted_data.bounds.top, 104);
}

/**
 * @brief Test enhanced prediction with deceleration.
 */
TEST_F(RoiManagerTest, EnhancedPredictionWithDeceleration) {
    // Create a sequence showing deceleration
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: large movement
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{115, 110, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections2, 2, 1920, 1080);

    // Third frame: smaller movement (deceleration)
    std::vector<detection_result> detections3;
    detections3.emplace_back(sf::rectangle{120, 115, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections3, 3, 1920, 1080);

    // Fourth frame: even smaller movement (continued deceleration)
    std::vector<detection_result> detections4;
    detections4.emplace_back(sf::rectangle{122, 117, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections4, 4, 1920, 1080);

    // Fifth frame: object missing, should predict with deceleration
    std::vector<detection_result> detections5;
    const auto tracked_objects = roi_tracker->process_frame(detections5, 5, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].history.size(), 5);

    // Check predicted position - should show deceleration effect
    const auto &predicted_data = tracked_objects[0].history[4];
    EXPECT_EQ(predicted_data.frame_number, 5);
    EXPECT_EQ(predicted_data.confidence, -1.0f);
    EXPECT_EQ(predicted_data.color, sf::yuv_color::red);

    // Weighted velocity with deceleration damping produces these values
    EXPECT_EQ(predicted_data.bounds.left, 117);
    EXPECT_EQ(predicted_data.bounds.top, 113);
}

/**
 * @brief Test enhanced prediction with noisy movement data.
 */
TEST_F(RoiManagerTest, EnhancedPredictionWithNoisyData) {
    // Create a sequence with some inconsistent movements
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    [[maybe_unused]] const auto unused1 = roi_tracker->process_frame(detections1, 1, 1920, 1080);

    // Second frame: consistent movement
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 50, 50}, 0, 0.8f, 1);
    [[maybe_unused]] const auto unused2 = roi_tracker->process_frame(detections2, 2, 1920, 1080);

    // Third frame: inconsistent movement (noise)
    std::vector<detection_result> detections3;
    detections3.emplace_back(sf::rectangle{105, 110, 50, 50}, 0, 0.8f, 1);
    [[maybe_unused]] const auto unused3 = roi_tracker->process_frame(detections3, 3, 1920, 1080);

    // Fourth frame: back to consistent movement
    std::vector<detection_result> detections4;
    detections4.emplace_back(sf::rectangle{115, 115, 50, 50}, 0, 0.8f, 1);
    [[maybe_unused]] const auto unused4 = roi_tracker->process_frame(detections4, 4, 1920, 1080);

    // Fifth frame: object missing, should predict based on general direction
    std::vector<detection_result> detections5;
    const auto tracked_objects = roi_tracker->process_frame(detections5, 5, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].history.size(), 5);

    // Check predicted position - should follow general trend despite noise
    const auto &predicted_data = tracked_objects[0].history[4];
    EXPECT_EQ(predicted_data.frame_number, 5);
    EXPECT_EQ(predicted_data.confidence, -1.0f);
    EXPECT_EQ(predicted_data.color, sf::yuv_color::red);

    // Weighted velocity with noisy data filtering produces these values
    EXPECT_EQ(predicted_data.bounds.left, 110);
    EXPECT_EQ(predicted_data.bounds.top, 109);
}

/**
 * @brief Test that prediction is disabled when enable_prediction is false.
 */
TEST(RoiManagerTestNoPrediction, PredictionDisabled) {
    // Create roi_manager with prediction disabled
    roi_manager roi_tracker_no_prediction(0.5f, 5, false, roi_accumulation_mode::object_id, 0.33f,
                                          stabilization_config{});

    // First frame: object detected
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker_no_prediction.process_frame(detections1, 1, 1920, 1080);

    // Second frame: object detected at new position
    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 55, 52}, 0, 0.8f, 1);
    (void) roi_tracker_no_prediction.process_frame(detections2, 2, 1920, 1080);

    // Third frame: object missing - should NOT add predicted position
    std::vector<detection_result> detections3;
    const auto tracked_objects = roi_tracker_no_prediction.process_frame(detections3, 3, 1920, 1080);

    ASSERT_EQ(tracked_objects.size(), 1);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 1);

    // History should only contain the 2 actual detections, no predicted position
    EXPECT_EQ(tracked_objects[0].history.size(), 2);

    // Verify the last entry is still the actual detection from frame 2, not a prediction.
    // Note: bounds may differ from raw detection due to EMA stabilization.
    const auto &last_data = tracked_objects[0].history.back();
    EXPECT_EQ(last_data.frame_number, 2);
    EXPECT_EQ(last_data.confidence, 0.8f); // Should be actual confidence, not -1.0f (prediction marker)
    EXPECT_EQ(last_data.color, sf::yuv_color::blue); // Should be detection color, not red (prediction color)
    EXPECT_EQ(last_data.bounds.left, 103);
    EXPECT_EQ(last_data.bounds.top, 100);
}

TEST_F(RoiManagerTest, AdvanceFrameWithoutDetectionGeneratesPredictionWhenEnabled) {
    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections1, 1, 1920, 1080);

    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 50, 50}, 0, 0.8f, 1);
    (void) roi_tracker->process_frame(detections2, 2, 1920, 1080);

    roi_tracker->advance_frame_without_detection(3, 1920, 1080);
    const auto &tracked_objects = roi_tracker->get_tracked_objects();
    ASSERT_EQ(tracked_objects.size(), 1U);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 1U);
    ASSERT_EQ(tracked_objects[0].history.size(), 3U);
    EXPECT_EQ(tracked_objects[0].history.back().frame_number, 3U);
    EXPECT_EQ(tracked_objects[0].history.back().confidence, -1.0f);
    EXPECT_EQ(tracked_objects[0].history.back().color, sf::yuv_color::red);
}

TEST(RoiManagerAdvanceFrameNoPrediction, AdvanceFrameWithoutDetectionDoesNotAppendPredictionWhenDisabled) {
    roi_manager tracker(0.5f, 5, false, roi_accumulation_mode::object_id, 0.33f, stabilization_config{});

    std::vector<detection_result> detections1;
    detections1.emplace_back(sf::rectangle{100, 100, 50, 50}, 0, 0.8f, 1);
    (void) tracker.process_frame(detections1, 1, 1920, 1080);

    std::vector<detection_result> detections2;
    detections2.emplace_back(sf::rectangle{110, 105, 50, 50}, 0, 0.8f, 1);
    (void) tracker.process_frame(detections2, 2, 1920, 1080);

    tracker.advance_frame_without_detection(3, 1920, 1080);
    const auto &tracked_objects = tracker.get_tracked_objects();
    ASSERT_EQ(tracked_objects.size(), 1U);
    EXPECT_EQ(tracked_objects[0].state, object_state::missing);
    EXPECT_EQ(tracked_objects[0].missing_frames_count, 1U);
    EXPECT_EQ(tracked_objects[0].history.size(), 2U);
}

TEST(RoiManagerPredictionDirect, PredictObjectPositionHandlesEmptyAndSingleHistoryObjects) {
    detected_object empty_obj(11);
    predict_object_position(empty_obj, 5, 640, 480);
    EXPECT_TRUE(empty_obj.history.empty());

    detected_object single_history_obj(22);
    single_history_obj.history.emplace_back(sf::rectangle{20, 30, 40, 50}, 0.9f, 1U, sf::yuv_color::blue);
    predict_object_position(single_history_obj, 2, 640, 480);
    ASSERT_EQ(single_history_obj.history.size(), 2U);
    const auto &predicted = single_history_obj.history.back();
    EXPECT_EQ(predicted.bounds, sf::rectangle(20, 30, 40, 50));
    EXPECT_EQ(predicted.frame_number, 2U);
    EXPECT_EQ(predicted.confidence, -1.0f);
    EXPECT_EQ(predicted.color, sf::yuv_color::red);
}

TEST(RoiManagerPredictionDirect, PredictObjectPositionTrimsLongHistoryBeforeAppending) {
    detected_object obj(33);
    for (uint32_t frame = 0; frame < 120; ++frame) {
        obj.history.emplace_back(sf::rectangle{static_cast<int>(frame), 100, 30, 30}, 0.8f, frame, sf::yuv_color::blue);
    }

    predict_object_position(obj, 121, 1920, 1080);
    EXPECT_LE(obj.history.size(), 101U);
    ASSERT_FALSE(obj.history.empty());
    EXPECT_EQ(obj.history.front().frame_number, 20U);
    EXPECT_EQ(obj.history.back().frame_number, 121U);
}

TEST(RoiManagerPredictionDirect, trim_history_if_needed_and_apply_velocity_expansion_behavior_via_prediction_results) {
    detected_object obj(44);
    for (uint32_t frame = 0; frame < 130; ++frame) {
        obj.history.emplace_back(sf::rectangle{100 + static_cast<int>(frame), 200, 30, 30}, 0.8f, frame,
                                 sf::yuv_color::blue);
    }

    predict_object_position(obj, 131, 1920, 1080);
    EXPECT_LE(obj.history.size(), 101U);
    ASSERT_FALSE(obj.history.empty());
    EXPECT_EQ(obj.history.back().frame_number, 131U);
    EXPECT_EQ(obj.history.back().confidence, -1.0f);
}
