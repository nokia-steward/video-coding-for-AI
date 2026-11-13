// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <string>
#include <string_view>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"

namespace {
    [[nodiscard]] bool is_x86_architecture() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] encoder_config make_valid_camera_config() {
        encoder_config config;
        config.input_type_value = input_type::camera;
        config.camera_device = "/dev/video0";
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        return config;
    }

    [[nodiscard]] encoder_config make_valid_udp_config() {
        encoder_config config;
        config.input_type_value = input_type::udp;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.udp.port = 5004;
        config.udp.encoding = "H264";
        config.udp.payload_type = 96U;
        config.udp.clock_rate = 90000U;
        config.udp.latency_ms = 100;
        return config;
    }

    void expect_validation_error_contains(const encoder_config &config, const std::string &expected_substring) {
        try {
            validate_configuration(config);
            GTEST_FAIL() << "Expected validate_configuration to throw";
        } catch (const std::runtime_error &error) {
            EXPECT_NE(std::string(error.what()).find(expected_substring), std::string::npos);
        }
    }
} // namespace

TEST(EncoderConfigValidator, RejectsMutuallyExclusiveRoiSources) {
    auto config = make_valid_camera_config();
    config.inference_engine = true;
    config.static_roi_json_file = "static.json";

    expect_validation_error_contains(config, "Only one ROI source may be active");
}

TEST(EncoderConfigValidator, RejectsObjectIdAccumulationWhenTrackerDisabledForInference) {
    auto config = make_valid_camera_config();
    config.inference_engine = true;
    config.detection.enable_tracker = false;
    config.detection.roi_accumulation_mode_value = roi_accumulation_mode::object_id;

    expect_validation_error_contains(config, "roi_accumulation_mode must be set to 'overlap'");
}

TEST(EncoderConfigValidator, RejectsOutOfRangeQpMapBackgroundDelta) {
    auto config = make_valid_camera_config();
    config.qp_map_background_delta = 52;

    expect_validation_error_contains(config, "qp_map_background_delta must be in range");
}

TEST(EncoderConfigValidator, RejectsPackRoisWhenBackgroundModeIsEnabled) {
    auto config = make_valid_camera_config();
    config.pack_rois = true;
    config.background_mode_value = background_mode::background_blur;

    expect_validation_error_contains(config, "pack_rois must be disabled when background_mode is not 'off'");
}

TEST(EncoderConfigValidator, RejectsQpMapDeltaBackgroundModeOnNonX86Systems) {
    auto config = make_valid_camera_config();
    config.background_mode_value = background_mode::qp_map_delta;

    constexpr std::string_view expected_error =
            "background_mode='qp_map_delta' is supported only on x86 desktop systems";
    if (is_x86_architecture()) {
        try {
            validate_configuration(config);
        } catch (const std::runtime_error &error) {
            EXPECT_EQ(std::string(error.what()).find(expected_error), std::string::npos);
        }
        return;
    }

    expect_validation_error_contains(config, std::string(expected_error));
}

TEST(EncoderConfigValidator, RejectsInvalidStabilizationAlphaRange) {
    auto config = make_valid_camera_config();
    config.stabilization.alpha = -0.01F;

    expect_validation_error_contains(config, "stabilization alpha must be between 0.0 and 1.0");
}

TEST(EncoderConfigValidator, RejectsCommunicationHostWithoutPort) {
    auto config = make_valid_camera_config();
    config.communication.host = "127.0.0.1";
    config.communication.port = 0;

    expect_validation_error_contains(config, "Communication port must be specified");
}

TEST(EncoderConfigValidator, RejectsUdpPayloadTypeAbove127) {
    auto config = make_valid_udp_config();
    config.udp.payload_type = 128U;

    expect_validation_error_contains(config, "udp payload_type must be between 0 and 127");
}

TEST(EncoderConfigValidator, RejectsNonPositiveLumaDownscaleFactor) {
    auto config = make_valid_camera_config();
    config.luma_downscale_factor = 0.0;

    expect_validation_error_contains(config, "luma_downscale_factor must be in range");
}

TEST(EncoderConfigValidator, RejectsAdaptiveLumaWithoutLumaAdaptation) {
    auto config = make_valid_camera_config();
    config.enable_luma_adaptation = false;
    config.enable_adaptive_luma_adaptation = true;
    config.input_analyzer_model_path = "/path/to/model.onnx";

    expect_validation_error_contains(config, "enable_adaptive_luma_adaptation requires enable_luma_adaptation");
}

TEST(EncoderConfigValidator, RejectsAdaptiveLumaWithEmptyAnalyzerPath) {
    auto config = make_valid_camera_config();
    config.enable_luma_adaptation = true;
    config.enable_adaptive_luma_adaptation = true;
    config.input_analyzer_model_path = "";

    expect_validation_error_contains(config,
                                     "enable_adaptive_luma_adaptation requires a non-empty input_analyzer_model_path");
}
