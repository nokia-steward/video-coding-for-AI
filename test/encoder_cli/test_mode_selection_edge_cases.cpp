// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/detection_modes/detection_factory.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/packing_modes/packing_factory.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/processing_modes/processing_factory.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/roi_strategy_snapshot.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_mode_selection_edge_cases.cpp
 * @brief Tests for edge cases in mode selection.
 *
 * Tests verify that:
 * - Default fallbacks work correctly
 * - Empty data is handled properly
 * - Edge cases don't cause crashes
 */

class ModeSelectionEdgeCasesTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }
};

/**
 * @brief Test that detection factory defaults to passthrough when no mode matches.
 */
TEST_F(ModeSelectionEdgeCasesTest, DetectionFactoryDefaultsToPassthrough) {
    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_detection_mode(snapshot);

    // Should default to passthrough (from detection_factory.cpp line 76-82)
    ASSERT_NE(active_mode, nullptr) << "Should return passthrough as default";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should default to passthrough mode";
}

/**
 * @brief Test that processing factory selects passthrough when packing and background/QP-map are off.
 */
TEST_F(ModeSelectionEdgeCasesTest, ProcessingFactorySelectsPassthroughWhenDisabled) {
    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_processing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should return passthrough when packing is disabled";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should select passthrough mode";
}

/**
 * @brief Test that packing factory returns nullptr when pack_rois is false.
 */
TEST_F(ModeSelectionEdgeCasesTest, PackingFactoryReturnsNullptrWhenDisabled) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = false;
    snapshot.packer_type_value = packer_type::low_delay;

    const auto *active_mode = get_active_packing_mode(snapshot);

    EXPECT_EQ(active_mode, nullptr) << "Should return nullptr when pack_rois is false";
}

/**
 * @brief Test that static mode is not selected with empty static ROI data.
 */
TEST_F(ModeSelectionEdgeCasesTest, StaticModeNotSelectedWithEmptyData) {
    roi_strategy_snapshot snapshot;
    snapshot.static_roi_available = false;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr);
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should fall back to passthrough when static data is empty";
}

/**
 * @brief Test that static mode is not selected when static ROI data is not loaded.
 */
TEST_F(ModeSelectionEdgeCasesTest, StaticModeNotSelectedWhenNotLoaded) {
    roi_strategy_snapshot snapshot;
    snapshot.static_roi_available = false;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr);
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should fall back to passthrough when static data is not loaded";
}

/**
 * @brief Test that inference mode takes precedence over static mode.
 */
TEST_F(ModeSelectionEdgeCasesTest, InferenceTakesPrecedenceOverStatic) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = true;
    snapshot.static_roi_available = true;

    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr);
    EXPECT_STREQ(active_mode->name, "inference") << "Inference should take precedence over static";
}

/**
 * @brief Test that background blur takes precedence over packing.
 */
TEST_F(ModeSelectionEdgeCasesTest, BackgroundBlurTakesPrecedenceOverPacking) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::background_blur;

    const auto *active_mode = get_active_processing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr);
    EXPECT_STREQ(active_mode->name, "background_blur") << "Background blur should take precedence";
}

/**
 * @brief Test that get_active_detection_mode returns only one mode even when multiple could match.
 */
TEST_F(ModeSelectionEdgeCasesTest, OnlyOneDetectionModeActive) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = true;
    snapshot.static_roi_available = true;

    // Verify that get_active_detection_mode returns only one mode
    const auto *active_mode = get_active_detection_mode(snapshot);
    ASSERT_NE(active_mode, nullptr) << "Should return a detection mode";
    EXPECT_STREQ(active_mode->name, "inference") << "Inference should be selected when both are enabled";

    // Note: Multiple modes' is_enabled functions may return true simultaneously,
    // but get_active_detection_mode returns only the first matching one
    // This is the expected behavior - the factory selects the first enabled mode
}

/**
 * @brief Test that packing mode is not selected when pack_rois is false regardless of packer_type.
 */
TEST_F(ModeSelectionEdgeCasesTest, PackingModeNotSelectedWhenDisabled) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = false;
    snapshot.packer_type_value = packer_type::random_access; // Type doesn't matter when disabled

    const auto *active_mode = get_active_packing_mode(snapshot);

    EXPECT_EQ(active_mode, nullptr) << "Should return nullptr when pack_rois is false, regardless of packer_type";
}

/**
 * @brief Test that processing mode handles both pack_rois false and background_mode off.
 */
TEST_F(ModeSelectionEdgeCasesTest, ProcessingModeHandlesBothDisabled) {
    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_processing_mode(snapshot);

    // No packing and no background/QP-map activity selects the explicit passthrough mode
    ASSERT_NE(active_mode, nullptr) << "Should return a processing mode";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should select passthrough mode";
}
