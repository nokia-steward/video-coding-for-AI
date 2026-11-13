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
 * @file test_roi_detection_processing_api.cpp
 * @brief Tests for the main ROI detection processing API coordination.
 *
 * Tests verify that:
 * - Mode selection and coordination works correctly
 * - Error handling and fallback behavior works
 */

class RoiDetectionProcessingApiTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }
};

/**
 * @brief Test that mode coordination works correctly through factories.
 */
TEST_F(RoiDetectionProcessingApiTest, ModeCoordinationWorks) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = false;
    snapshot.static_roi_available = true;
    snapshot.packer_type_value = packer_type::low_delay;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::off;

    // Verify all modes can be retrieved
    const auto *detection_mode = get_active_detection_mode(snapshot);
    ASSERT_NE(detection_mode, nullptr) << "Should have active detection mode";
    EXPECT_STREQ(detection_mode->name, "static") << "Should select static mode";

    const auto *packing_mode = get_active_packing_mode(snapshot);
    ASSERT_NE(packing_mode, nullptr) << "Should have active packing mode";
    EXPECT_STREQ(packing_mode->name, "low_delay") << "Should select low_delay mode";

    const auto *processing_mode = get_active_processing_mode(snapshot);
    ASSERT_NE(processing_mode, nullptr) << "Should have active processing mode";
    EXPECT_STREQ(processing_mode->name, "packing") << "Should select packing mode";
}

/**
 * @brief Test that passthrough mode coordination works correctly.
 */
TEST_F(RoiDetectionProcessingApiTest, PassthroughModeCoordination) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = false;
    snapshot.static_roi_available = false;
    snapshot.pack_rois = false; // No packing
    snapshot.effective_background_mode_value = background_mode::off;

    // Verify passthrough mode is selected
    const auto *detection_mode = get_active_detection_mode(snapshot);
    ASSERT_NE(detection_mode, nullptr) << "Should have active detection mode";
    EXPECT_STREQ(detection_mode->name, "passthrough") << "Should select passthrough mode";

    // Verify processing mode is still selected (packing mode for no ROIs)
    const auto *processing_mode = get_active_processing_mode(snapshot);
    ASSERT_NE(processing_mode, nullptr) << "Should have active processing mode";
}

/**
 * @brief Test that background blur mode coordination works correctly.
 */
TEST_F(RoiDetectionProcessingApiTest, BackgroundBlurModeCoordination) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = false;
    snapshot.static_roi_available = true;
    snapshot.pack_rois = false; // Background blur disables packing
    snapshot.effective_background_mode_value = background_mode::background_blur;

    // Verify background blur processing mode is selected
    const auto *processing_mode = get_active_processing_mode(snapshot);
    ASSERT_NE(processing_mode, nullptr) << "Should have active processing mode";
    EXPECT_STREQ(processing_mode->name, "background_blur") << "Should select background_blur mode";

    // Verify detection mode is still selected
    const auto *detection_mode = get_active_detection_mode(snapshot);
    ASSERT_NE(detection_mode, nullptr) << "Should have active detection mode";
    EXPECT_STREQ(detection_mode->name, "static") << "Should select static mode";
}

/**
 * @brief Test that random_access packing mode coordination works correctly.
 */
TEST_F(RoiDetectionProcessingApiTest, RandomAccessPackingCoordination) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = false;
    snapshot.static_roi_available = true;
    snapshot.packer_type_value = packer_type::random_access;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::off;

    // Verify random_access packing mode is selected
    const auto *packing_mode = get_active_packing_mode(snapshot);
    ASSERT_NE(packing_mode, nullptr) << "Should have active packing mode";
    EXPECT_STREQ(packing_mode->name, "random_access") << "Should select random_access mode";

    // Verify processing mode is still selected
    const auto *processing_mode = get_active_processing_mode(snapshot);
    ASSERT_NE(processing_mode, nullptr) << "Should have active processing mode";
}

/**
 * @brief Test that mode coordination handles frames with no ROIs correctly.
 */
TEST_F(RoiDetectionProcessingApiTest, ModeCoordinationHandlesNoRois) {
    roi_strategy_snapshot snapshot;
    snapshot.inference_engine = false;
    snapshot.static_roi_available = false;
    snapshot.packer_type_value = packer_type::low_delay;
    snapshot.pack_rois = true;
    snapshot.effective_background_mode_value = background_mode::off;

    // Verify passthrough mode is selected (no ROIs)
    const auto *detection_mode = get_active_detection_mode(snapshot);
    ASSERT_NE(detection_mode, nullptr) << "Should have active detection mode";
    EXPECT_STREQ(detection_mode->name, "passthrough") << "Should select passthrough mode";

    // Verify processing mode can handle no ROIs
    const auto *processing_mode = get_active_processing_mode(snapshot);
    ASSERT_NE(processing_mode, nullptr) << "Should have active processing mode";
    EXPECT_NE(processing_mode->functions.process_no_rois, nullptr) << "Should have process_no_rois function";
}
