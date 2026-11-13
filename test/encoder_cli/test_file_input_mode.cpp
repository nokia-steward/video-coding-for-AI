// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/input_modes/file/file_input_mode.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/file_input.h"
#include "../../encoder_cli/src/gst/input_types/roi_input.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_64x64_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }
} // namespace

class FileInputModeTest : public GStreamerTestFixture {};

TEST_F(FileInputModeTest, IsEnabledOnlyForFileInputType) {
    const auto functions = create_file_input_mode_functions();

    auto config_file = make_base_config();
    config_file.input_type_value = input_type::file;
    const global_data file_data(std::move(config_file));
    EXPECT_TRUE(functions.is_enabled(file_data));

    auto config_camera = make_base_config();
    config_camera.input_type_value = input_type::camera;
    const global_data camera_data(std::move(config_camera));
    EXPECT_FALSE(functions.is_enabled(camera_data));

    auto config_udp = make_base_config();
    config_udp.input_type_value = input_type::udp;
    const global_data udp_data(std::move(config_udp));
    EXPECT_FALSE(functions.is_enabled(udp_data));
}

TEST_F(FileInputModeTest, PipelineCreatorSwitchesBetweenFileAndRoiDetection) {
    const auto functions = create_file_input_mode_functions();

    auto config_file = make_base_config();
    config_file.inference_engine = false;
    const global_data file_data(std::move(config_file));
    const auto file_creator = functions.get_pipeline_creator(file_data);
    ASSERT_TRUE(file_creator.has_value());
    const auto file_target = file_creator->target<bool (*)(global_data &)>();
    ASSERT_NE(file_target, nullptr);
    EXPECT_EQ(*file_target, &create_file_input_pipeline);

    auto config_roi = make_base_config();
    config_roi.inference_engine = true;
    const global_data roi_data(std::move(config_roi));
    const auto roi_creator = functions.get_pipeline_creator(roi_data);
    ASSERT_TRUE(roi_creator.has_value());
    const auto roi_target = roi_creator->target<bool (*)(global_data &)>();
    ASSERT_NE(roi_target, nullptr);
    EXPECT_EQ(*roi_target, &create_roi_detection_input_pipeline);
}

TEST_F(FileInputModeTest, ContextExposesExpectedModeNameAndCallbacks) {
    const auto context = create_file_input_mode_context();

    ASSERT_NE(context.functions.is_enabled, nullptr);
    ASSERT_NE(context.functions.get_pipeline_creator, nullptr);
    ASSERT_NE(context.functions.run_mode, nullptr);
    ASSERT_NE(context.functions.get_mode_name, nullptr);
    EXPECT_STREQ(context.functions.get_mode_name(), "file");
}
