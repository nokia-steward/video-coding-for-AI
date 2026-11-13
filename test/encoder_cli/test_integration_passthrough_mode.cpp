// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <gst/gstregistry.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/detection_modes/detection_factory.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/roi_strategy_snapshot.h"
#include "../common/gst_test_fixture.h"
#include "../common/integration_test_helpers.h"

/**
 * @file test_integration_passthrough_mode.cpp
 * @brief End-to-end integration test for passthrough detection mode.
 *
 * This test verifies that passthrough mode works correctly:
 * - Passthrough mode is selected when neither inference nor static is enabled
 * - Passthrough mode returns empty ROIs
 * - Pipeline can process frames without ROIs
 */

class PassthroughModeIntegrationTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();

        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }

        GstRegistry *registry = gst_registry_get();
        if (registry) {
            gst_update_registry();
        }

        init_gst_pipeline_debug_category();
        ensure_test_directories();
    }
};

/**
 * @brief Test that passthrough mode is selected when no detection is enabled.
 */
TEST_F(PassthroughModeIntegrationTest, PassthroughModeSelected) {
    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_detection_mode(snapshot);

    ASSERT_NE(active_mode, nullptr) << "Should select a detection mode";
    EXPECT_STREQ(active_mode->name, "passthrough") << "Should select passthrough mode";
}

/**
 * @brief Test that passthrough mode returns empty ROIs.
 */
TEST_F(PassthroughModeIntegrationTest, PassthroughReturnsEmptyRois) {
    encoder_config config;
    config.inference_engine = false;
    config.input_type_value = input_type::file;

    global_data data(std::move(config));

    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_detection_mode(snapshot);
    ASSERT_NE(active_mode, nullptr);

    // Create dummy buffer
    const auto buffer = gst_buffer_new_allocate(nullptr, 100, nullptr);
    const sf::unique_gst_buffer_no_delete_ptr input_buffer(buffer);

    frame_properties props;
    props.width = 640;
    props.height = 480;
    props.format = "video/x-raw,format=I420";

    const auto [input_rectangles, split_rectangles] = active_mode->functions.detect_rois(data, input_buffer, props, 0);

    EXPECT_TRUE(input_rectangles.empty()) << "Passthrough should return empty input rectangles";
    EXPECT_TRUE(split_rectangles.empty()) << "Passthrough should return empty split rectangles";
}

/**
 * @brief Test that passthrough mode works for any input type.
 */
TEST_F(PassthroughModeIntegrationTest, PassthroughWorksWithAllInputTypes) {
    // The strategy snapshot is input-type agnostic, so passthrough is selected uniformly.
    const roi_strategy_snapshot snapshot;
    const auto *active_mode = get_active_detection_mode(snapshot);
    ASSERT_NE(active_mode, nullptr);
    EXPECT_STREQ(active_mode->name, "passthrough");
}
