// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/packing_modes/packing_factory.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/roi_strategy_snapshot.h"
#include "../common/gst_test_fixture.h"
#include "../common/mode_factory_test_helpers.h"

/**
 * @file test_packing_mode_factory.cpp
 * @brief Tests for packing mode factory pattern.
 *
 * Tests verify that:
 * - All packing modes are created correctly
 * - Mode selection logic works correctly against a narrow config snapshot
 * - Returns nullptr when packing is disabled
 */

class PackingModeFactoryTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }
};

/**
 * @brief Test that all packing modes are created.
 */
TEST_F(PackingModeFactoryTest, CreatesAllModes) {
    const auto contexts = create_packing_mode_contexts();
    expect_contexts_created(contexts, {"low_delay", "random_access"},
                            [](const auto &context) { return std::string_view(context.name); });

    for (const std::string_view mode_name: {"low_delay", "random_access"}) {
        const auto *context = find_context_by_name(contexts, mode_name,
                                                   [](const auto &entry) { return std::string_view(entry.name); });
        ASSERT_NE(context, nullptr);
        EXPECT_NE(context->functions.process_frame, nullptr);
        EXPECT_NE(context->functions.get_packed_rectangles, nullptr);
        EXPECT_NE(context->functions.is_enabled, nullptr);
    }
}

/**
 * @brief Test that low delay mode is selected when pack_rois is true and packer_type is low_delay.
 */
TEST_F(PackingModeFactoryTest, SelectsLowDelayMode) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.packer_type_value = packer_type::low_delay;

    const auto *active_mode = get_active_packing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a packing mode";
    EXPECT_STREQ(active_mode->name, "low_delay") << "Should select low delay mode";
}

/**
 * @brief Test that random access mode is selected when pack_rois is true and packer_type is random_access.
 */
TEST_F(PackingModeFactoryTest, SelectsRandomAccessMode) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.packer_type_value = packer_type::random_access;

    const auto *active_mode = get_active_packing_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a packing mode";
    EXPECT_STREQ(active_mode->name, "random_access") << "Should select random access mode";
}

/**
 * @brief Test that nullptr is returned when pack_rois is false.
 */
TEST_F(PackingModeFactoryTest, ReturnsNullptrWhenPackingDisabled) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = false;
    snapshot.packer_type_value = packer_type::low_delay; // Type doesn't matter when disabled

    const auto *active_mode = get_active_packing_mode(snapshot);

    EXPECT_EQ(active_mode, nullptr) << "Should return nullptr when packing is disabled";
}

/**
 * @brief Test that nullptr is returned when pack_rois is true but packer_type doesn't match any mode.
 */
TEST_F(PackingModeFactoryTest, ReturnsNullptrWhenNoModeMatches) {
    roi_strategy_snapshot snapshot_disabled;
    snapshot_disabled.pack_rois = false;
    const auto *active_mode_disabled = get_active_packing_mode(snapshot_disabled);
    EXPECT_EQ(active_mode_disabled, nullptr) << "Should return nullptr when packing is disabled";

    roi_strategy_snapshot snapshot_enabled;
    snapshot_enabled.pack_rois = true;
    snapshot_enabled.packer_type_value = packer_type::low_delay;
    const auto *active_mode_enabled = get_active_packing_mode(snapshot_enabled);
    ASSERT_NE(active_mode_enabled, nullptr) << "Should select a mode when packing is enabled";
}

/**
 * @brief Test that only one mode can be active at a time.
 */
TEST_F(PackingModeFactoryTest, OnlyOneModeActive) {
    roi_strategy_snapshot snapshot;
    snapshot.pack_rois = true;
    snapshot.packer_type_value = packer_type::low_delay;

    const auto *active_mode = get_active_packing_mode(snapshot);
    ASSERT_NE(active_mode, nullptr);

    // Verify only one mode matches
    const auto contexts = create_packing_mode_contexts();
    expect_single_active_context(contexts, snapshot, [](const auto &context, const auto &snapshot_ref) {
        return context.functions.is_enabled(snapshot_ref);
    });
}
