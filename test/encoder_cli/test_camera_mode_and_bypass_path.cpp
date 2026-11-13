// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/input_modes/camera/camera_input_mode.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/bypass_path.h"
#include "../../encoder_cli/src/gst/input_types/camera_input.h"
#include "../../encoder_cli/src/gst/input_types/roi_input.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::camera;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.format = sf::video_format::i420;
        config.camera_device = "/dev/video0";
        return config;
    }
} // namespace

class CameraModeAndBypassPathTest : public GStreamerTestFixture {};

TEST_F(CameraModeAndBypassPathTest, CameraInputModeEnablementAndPipelineSelectionAreDeterministic) {
    const auto functions = create_camera_input_mode_functions();

    auto config_camera = make_base_config();
    config_camera.input_type_value = input_type::camera;
    global_data camera_data(std::move(config_camera));
    EXPECT_TRUE(functions.is_enabled(camera_data));

    auto config_file = make_base_config();
    config_file.input_type_value = input_type::file;
    config_file.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
    global_data file_data(std::move(config_file));
    EXPECT_FALSE(functions.is_enabled(file_data));

    auto config_non_inference = make_base_config();
    config_non_inference.inference_engine = false;
    global_data non_inference_data(std::move(config_non_inference));
    const auto camera_creator = functions.get_pipeline_creator(non_inference_data);
    ASSERT_TRUE(camera_creator.has_value());
    const auto camera_target = camera_creator->target<bool (*)(global_data &)>();
    ASSERT_NE(camera_target, nullptr);
    EXPECT_EQ(*camera_target, &create_camera_input_pipeline);

    auto config_inference = make_base_config();
    config_inference.inference_engine = true;
    global_data inference_data(std::move(config_inference));
    const auto roi_creator = functions.get_pipeline_creator(inference_data);
    ASSERT_TRUE(roi_creator.has_value());
    const auto roi_target = roi_creator->target<bool (*)(global_data &)>();
    ASSERT_NE(roi_target, nullptr);
    EXPECT_EQ(*roi_target, &create_roi_detection_input_pipeline);

    const auto context = create_camera_input_mode_context();
    EXPECT_STREQ(context.functions.get_mode_name(), "camera");
}

TEST_F(CameraModeAndBypassPathTest, BypassPathGuardsAndConfigurationBehaveAsExpected) {
    init_bypass_path_debug_category();

    bypass_path_elements elements{};
    EXPECT_FALSE(add_bypass_path_to_pipeline(nullptr, elements));
    EXPECT_FALSE(link_tee_to_bypass_path(nullptr, elements));
    EXPECT_FALSE(link_bypass_path_elements(elements));

    const bool created = create_bypass_path_elements(elements);
    if (!created) {
        GTEST_SKIP() << "Required bypass path plugins unavailable in this environment";
    }

    auto config = make_base_config();
    global_data data(std::move(config));
    ASSERT_TRUE(configure_bypass_path_elements(elements, data.params));

    sf::unique_gst_element_ptr pipeline(gst_pipeline_new("test-bypass-path-pipeline"));
    ASSERT_NE(pipeline, nullptr);
    ASSERT_TRUE(add_bypass_path_to_pipeline(pipeline.get(), elements));
    ASSERT_TRUE(link_bypass_path_elements(elements));

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "test-bypass-tee"));
    ASSERT_NE(tee, nullptr);
    gst_bin_add(GST_BIN(pipeline.get()), GST_ELEMENT(gst_object_ref(tee.get())));
    EXPECT_TRUE(link_tee_to_bypass_path(tee.get(), elements));
}
