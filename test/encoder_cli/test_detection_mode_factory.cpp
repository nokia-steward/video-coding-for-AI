// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/processing/roi_detection_processing/detection_modes/detection_factory.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/roi_strategy_snapshot.h"
#include "../common/gst_test_fixture.h"
#include "../common/mode_factory_test_helpers.h"

/**
 * @file test_detection_mode_factory.cpp
 * @brief Tests for detection mode factory pattern.
 *
 * Tests verify that:
 * - All detection modes are created correctly
 * - Mode selection logic works correctly against a narrow config snapshot
 * - Edge cases are handled properly
 */

class DetectionModeFactoryTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }
};

/**
 * @brief Test that all detection modes are created.
 */
TEST_F(DetectionModeFactoryTest, CreatesAllModes) {
    const auto contexts = create_detection_mode_contexts();
    expect_contexts_created(contexts, {"inference", "static", "passthrough", "groundtruth"},
                            [](const auto &context) { return std::string_view(context.name); });

    for (const std::string_view mode_name: {"inference", "static", "passthrough", "groundtruth"}) {
        const auto *context = find_context_by_name(contexts, mode_name,
                                                   [](const auto &entry) { return std::string_view(entry.name); });
        ASSERT_NE(context, nullptr);
        EXPECT_NE(context->functions.detect_rois, nullptr);
        EXPECT_NE(context->functions.is_enabled, nullptr);
        EXPECT_NE(context->functions.get_tracked_objects, nullptr);
    }
}

/**
 * @brief Test that inference mode is selected when inference_engine is enabled.
 */
TEST_F(DetectionModeFactoryTest, SelectsInferenceMode) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = true;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a detection mode";
    EXPECT_STREQ(active_mode->name, "inference") << "Should select inference mode";
}

/**
 * @brief Test that static mode is selected when static ROI data is loaded.
 */
TEST_F(DetectionModeFactoryTest, SelectsStaticMode) {
    roi_strategy_snapshot snapshot;
    snapshot.static_roi_available = true;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a detection mode";
    EXPECT_STREQ(active_mode->name, "static") << "Should select static mode";
}

/**
 * @brief Test that passthrough mode is selected when neither inference nor static is enabled.
 */
TEST_F(DetectionModeFactoryTest, SelectsPassthroughMode) {
    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a detection mode";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should select passthrough mode";
}

/**
 * @brief Test that inference mode takes precedence over static mode.
 */
TEST_F(DetectionModeFactoryTest, InferenceTakesPrecedenceOverStatic) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = true;
    snapshot.static_roi_available = true;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a detection mode";
    EXPECT_STREQ(active_mode->name, "inference") << "Inference should take precedence over static";
}

/**
 * @brief Test that static mode is not selected when static ROI data is empty.
 */
TEST_F(DetectionModeFactoryTest, StaticModeNotSelectedWithEmptyData) {
    roi_strategy_snapshot snapshot;
    snapshot.static_roi_available = false;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a detection mode";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should fall back to passthrough when static data is empty";
}

/**
 * @brief Test that static mode is not selected when static ROI data is not loaded.
 */
TEST_F(DetectionModeFactoryTest, StaticModeNotSelectedWhenNotLoaded) {
    roi_strategy_snapshot snapshot;
    snapshot.static_roi_available = false;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a detection mode";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should fall back to passthrough when static data is not loaded";
}

/**
 * @brief Test that passthrough mode is returned as default when no mode matches.
 */
TEST_F(DetectionModeFactoryTest, DefaultsToPassthrough) {
    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_detection_mode(snapshot);

    // Should default to passthrough (implementation detail from detection_factory.cpp)
    ASSERT_NE(active_mode, nullptr) << "Should return passthrough as default";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should default to passthrough mode";
}
