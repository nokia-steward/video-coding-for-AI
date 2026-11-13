// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file test_get_luma_factors.cpp
 * @brief Unit tests for get_luma_factors_for_frame fallback paths (no ONNX model required).
 */

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/input_analyzer/input_analyzer.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../common/gst_test_fixture.h"

namespace {

    encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        return config;
    }

    frame_properties make_dummy_props() {
        frame_properties props{};
        props.width = 640;
        props.height = 480;
        props.format = "I420";
        return props;
    }

} // namespace

class GetLumaFactorsTest : public GStreamerTestFixture {};

TEST_F(GetLumaFactorsTest, LumaDisabledReturnsOneAndUpdatesLastApplied) {
    auto config = make_base_config();
    config.enable_luma_adaptation = false;
    config.luma_downscale_factor = 0.6;
    global_data data(std::move(config));
    const frame_properties props = make_dummy_props();

    const luma_factors_for_frame result = get_luma_factors_for_frame(data, nullptr, props);

    EXPECT_DOUBLE_EQ(result.downscale_factor, 1.0);
    EXPECT_FALSE(result.upscale_multiplier.has_value());
    EXPECT_DOUBLE_EQ(data.last_applied_luma_downscale.load(), 1.0);
    EXPECT_DOUBLE_EQ(data.last_applied_luma_upscale.load(), 1.0);
}

TEST_F(GetLumaFactorsTest, FixedLumaReturnsConfiguredFactorAndUpdatesLastApplied) {
    auto config = make_base_config();
    config.enable_luma_adaptation = true;
    config.enable_adaptive_luma_adaptation = false;
    config.luma_downscale_factor = 0.55;
    global_data data(std::move(config));
    const frame_properties props = make_dummy_props();

    const luma_factors_for_frame result = get_luma_factors_for_frame(data, nullptr, props);

    EXPECT_DOUBLE_EQ(result.downscale_factor, 0.55);
    EXPECT_FALSE(result.upscale_multiplier.has_value());
    EXPECT_DOUBLE_EQ(data.last_applied_luma_downscale.load(), 0.55);
    EXPECT_DOUBLE_EQ(data.last_applied_luma_upscale.load(), 1.0);
}

TEST_F(GetLumaFactorsTest, AdaptiveEnabledAnalyzerNullUsesFixedPath) {
    // Initialize input_analyzer debug category so fallback warning can log safely
    {
        auto tmp = input_analyzer_create();
        ASSERT_NE(tmp, nullptr);
        static_cast<void>(input_analyzer_init(tmp.get(), "/nonexistent/model.onnx"));
    }
    auto config = make_base_config();
    config.enable_luma_adaptation = true;
    config.enable_adaptive_luma_adaptation = true;
    config.input_analyzer_model_path = "/nonexistent/model.onnx";
    config.luma_downscale_factor = 0.5;
    global_data data(std::move(config));
    // adaptive_luma_analyzer remains nullptr (never created)
    const frame_properties props = make_dummy_props();

    const luma_factors_for_frame result = get_luma_factors_for_frame(data, nullptr, props);

    EXPECT_DOUBLE_EQ(result.downscale_factor, 0.5);
    EXPECT_FALSE(result.upscale_multiplier.has_value());
    EXPECT_DOUBLE_EQ(data.last_applied_luma_downscale.load(), 0.5);
    EXPECT_DOUBLE_EQ(data.last_applied_luma_upscale.load(), 1.0);
}

TEST_F(GetLumaFactorsTest, AdaptiveEnabledAnalyzerNotValidUsesFixedPath) {
    auto config = make_base_config();
    config.enable_luma_adaptation = true;
    config.enable_adaptive_luma_adaptation = true;
    config.input_analyzer_model_path = "/nonexistent/model.onnx";
    config.luma_downscale_factor = 0.7;
    global_data data(std::move(config));
    // Create analyzer but do not init (or init with bad path) so it is not valid
    auto analyzer = input_analyzer_create();
    ASSERT_NE(analyzer, nullptr);
    EXPECT_FALSE(input_analyzer_init(analyzer.get(), "/nonexistent/model.onnx"));
    EXPECT_FALSE(input_analyzer_is_valid(analyzer.get()));
    data.adaptive_luma_analyzer = std::move(analyzer);
    const frame_properties props = make_dummy_props();

    const luma_factors_for_frame result = get_luma_factors_for_frame(data, nullptr, props);

    EXPECT_DOUBLE_EQ(result.downscale_factor, 0.7);
    EXPECT_FALSE(result.upscale_multiplier.has_value());
    EXPECT_DOUBLE_EQ(data.last_applied_luma_downscale.load(), 0.7);
    EXPECT_DOUBLE_EQ(data.last_applied_luma_upscale.load(), 1.0);
}
