// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/processing_modes/processing_factory.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/roi_strategy_snapshot.h"
#include "../common/gst_test_fixture.h"
#include "../common/mode_factory_test_helpers.h"

/**
 * @file test_processing_mode_factory.cpp
 * @brief Tests for processing mode factory pattern.
 *
 * Tests verify that:
 * - All processing modes are created correctly
 * - Mode selection logic works correctly against a narrow config snapshot
 * - Background mode takes precedence over packing
 * - Defaults to packing when no mode explicitly enabled
 */

class ProcessingModeFactoryTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }
};

/**
 * @brief Test that all processing modes are created.
 */
TEST_F(ProcessingModeFactoryTest, CreatesAllModes) {
    const auto contexts = create_processing_mode_contexts();
    expect_contexts_created(contexts, {"packing", "background_blur", "qp_map_delta", "passthrough"},
                            [](const auto &context) { return std::string_view(context.name); });

    for (const std::string_view mode_name: {"packing", "background_blur", "qp_map_delta", "passthrough"}) {
        const auto *context = find_context_by_name(contexts, mode_name,
                                                   [](const auto &entry) { return std::string_view(entry.name); });
        ASSERT_NE(context, nullptr);
        EXPECT_NE(context->functions.process_frame, nullptr);
        EXPECT_NE(context->functions.process_no_rois, nullptr);
        EXPECT_NE(context->functions.is_enabled, nullptr);
    }
}

/**
 * @brief Test that packing mode is selected when pack_rois is true and background_mode is off.
 */
TEST_F(ProcessingModeFactoryTest, SelectsPackingMode) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::off;

    const auto *active_mode = get_active_processing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a processing mode";
    EXPECT_STREQ(active_mode->name, "packing") << "Should select packing mode";
}

/**
 * @brief Test that background blur mode is selected when background_blur is true.
 */
TEST_F(ProcessingModeFactoryTest, SelectsBackgroundBlurMode) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true; // Packing enabled but should be overridden
    snapshot.effective_background_mode_value = background_mode::background_blur;

    const auto *active_mode = get_active_processing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a processing mode";
    EXPECT_STREQ(active_mode->name, "background_blur") << "Should select background blur mode";
}

/**
 * @brief Test that background blur takes precedence over packing.
 */
TEST_F(ProcessingModeFactoryTest, BackgroundBlurTakesPrecedence) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::background_blur;

    const auto *active_mode = get_active_processing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a processing mode";
    EXPECT_STREQ(active_mode->name, "background_blur") << "Background blur should take precedence over packing";
}

/**
 * @brief Test that packing mode is selected when background mode is off and pack_rois is true.
 */
TEST_F(ProcessingModeFactoryTest, PackingModeWhenBlurDisabled) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::off;

    const auto *active_mode = get_active_processing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a processing mode";
    EXPECT_STREQ(active_mode->name, "packing") << "Should select packing when blur is disabled";
}

/**
 * @brief Test that passthrough mode is selected when packing is disabled and no background/QP-map mode is active.
 */
TEST_F(ProcessingModeFactoryTest, SelectsPassthroughWhenPackingDisabled) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = false;
    snapshot.effective_background_mode_value = background_mode::off;

    const auto *active_mode = get_active_processing_mode(snapshot);

    // The no-ROI disabled-packing path is explicit: passthrough, not an implicit packing default.
    ASSERT_NE(active_mode, nullptr) << "Should select a processing mode";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should select passthrough when packing is disabled";
}

/**
 * @brief Test that passthrough mode is registered with the expected is_enabled condition.
 */
TEST_F(ProcessingModeFactoryTest, PassthroughIsEnabledCondition) {
    const auto contexts = create_processing_mode_contexts();
    const auto *passthrough = find_context_by_name(contexts, "passthrough",
                                                   [](const auto &entry) { return std::string_view(entry.name); });
    ASSERT_NE(passthrough, nullptr);

    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = false;
    snapshot.effective_background_mode_value = background_mode::off;
    EXPECT_TRUE(passthrough->functions.is_enabled(snapshot))
            << "Passthrough enabled when packing off and no background/QP-map processing";

    snapshot.pack_rois = true;
    EXPECT_FALSE(passthrough->functions.is_enabled(snapshot)) << "Not enabled when packing is on";

    snapshot.pack_rois = false;
    snapshot.effective_background_mode_value = background_mode::background_blur;
    EXPECT_FALSE(passthrough->functions.is_enabled(snapshot)) << "Not enabled when background processing is on";

    snapshot.effective_background_mode_value = background_mode::qp_map_delta;
    EXPECT_FALSE(passthrough->functions.is_enabled(snapshot)) << "Not enabled when QP-map delta is effective";
}

/**
 * @brief Test that passthrough mode delegates per-frame and no-ROI handling to the no-ROI packing behavior.
 */
TEST_F(ProcessingModeFactoryTest, PassthroughDelegatesToNoRoiPacking) {
    const auto contexts = create_processing_mode_contexts();
    const auto *passthrough = find_context_by_name(contexts, "passthrough",
                                                   [](const auto &entry) { return std::string_view(entry.name); });
    ASSERT_NE(passthrough, nullptr);
    EXPECT_NE(passthrough->functions.process_frame, nullptr);
    EXPECT_NE(passthrough->functions.process_no_rois, nullptr);
}

/**
 * @brief Test that only one mode can be active at a time.
 */
TEST_F(ProcessingModeFactoryTest, OnlyOneModeActive) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::off;

    const auto *active_mode = get_active_processing_mode(snapshot);
    ASSERT_NE(active_mode, nullptr);

    // Verify only one mode matches
    const auto contexts = create_processing_mode_contexts();
    expect_single_active_context(contexts, snapshot, [](const auto &context, const auto &snapshot_ref) {
        return context.functions.is_enabled(snapshot_ref);
    });
}

/**
 * @brief Test that background blur disables packing.
 */
TEST_F(ProcessingModeFactoryTest, BackgroundBlurDisablesPacking) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::background_blur;

    // Verify packing mode is not enabled
    const auto contexts = create_processing_mode_contexts();
    const auto *packing_context =
            find_context_by_name(contexts, "packing", [](const auto &entry) { return std::string_view(entry.name); });
    const auto *blur_context = find_context_by_name(contexts, "background_blur",
                                                    [](const auto &entry) { return std::string_view(entry.name); });

    ASSERT_NE(packing_context, nullptr);
    ASSERT_NE(blur_context, nullptr);

    const bool packing_enabled = packing_context->functions.is_enabled(snapshot);
    const bool blur_enabled = blur_context->functions.is_enabled(snapshot);

    EXPECT_FALSE(packing_enabled) << "Packing should be disabled when background blur is enabled";
    EXPECT_TRUE(blur_enabled) << "Background blur should be enabled";
}

TEST_F(ProcessingModeFactoryTest, SelectsQpMapDeltaMode) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = false;
    snapshot.effective_background_mode_value = background_mode::qp_map_delta;
    const auto *active_mode = get_active_processing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a processing mode";
    EXPECT_STREQ(active_mode->name, "qp_map_delta") << "Should select qp_map_delta mode";
}
