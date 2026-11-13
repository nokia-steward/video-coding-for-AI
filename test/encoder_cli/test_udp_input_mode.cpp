// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/input_modes/udp/udp_input_mode.h"
#include "../../encoder_cli/src/application/lifecycle_facade.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/roi_input.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::udp;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.udp.port = 5004;
        config.udp.bind_address = "0.0.0.0";
        config.udp.encoding = "H265";
        config.udp.payload_type = 96;
        config.udp.clock_rate = 90000;
        config.udp.latency_ms = 200;
        return config;
    }
} // namespace

class UdpInputModeTest : public GStreamerTestFixture {};

TEST_F(UdpInputModeTest, IsEnabledOnlyForUdpInputType) {
    const auto functions = create_udp_input_mode_functions();

    auto config_udp = make_base_config();
    config_udp.input_type_value = input_type::udp;
    const global_data udp_data(std::move(config_udp));
    EXPECT_TRUE(functions.is_enabled(udp_data));

    auto config_file = make_base_config();
    config_file.input_type_value = input_type::file;
    const global_data file_data(std::move(config_file));
    EXPECT_FALSE(functions.is_enabled(file_data));

    auto config_camera = make_base_config();
    config_camera.input_type_value = input_type::camera;
    const global_data camera_data(std::move(config_camera));
    EXPECT_FALSE(functions.is_enabled(camera_data));
}

TEST_F(UdpInputModeTest, PipelineCreatorAlwaysUsesRoiDetectionPipeline) {
    const auto functions = create_udp_input_mode_functions();

    auto config = make_base_config();
    config.inference_engine = false;
    const global_data data(std::move(config));
    const auto creator = functions.get_pipeline_creator(data);
    ASSERT_TRUE(creator.has_value());
    const auto target = creator->target<bool (*)(global_data &)>();
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(*target, &create_roi_detection_input_pipeline);
}

TEST_F(UdpInputModeTest, RunModeReturnsImmediatelyWhenStopAlreadyRequested) {
    const auto functions = create_udp_input_mode_functions();
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);

    lifecycle_facade facade(data);
    const auto started_at = std::chrono::steady_clock::now();
    functions.run_mode(facade);
    const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at)
                    .count();
    EXPECT_LT(elapsed_ms, 250);
}
