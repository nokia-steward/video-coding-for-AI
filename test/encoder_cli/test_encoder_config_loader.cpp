// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {
    [[nodiscard]] std::filesystem::path get_temp_config_path(const std::string &file_name) {
        const auto dir = std::filesystem::temp_directory_path() / "vcai_encoder_config_loader_tests";
        std::filesystem::create_directories(dir);
        return dir / file_name;
    }

    void write_config_file(const std::filesystem::path &path, const std::string &contents) {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open());
        file << contents;
        file.flush();
        file.close();
    }
} // namespace

TEST(EncoderConfigLoader, ParsesLabelsAndDisablesPackRoisWhenBackgroundModeEnabled) {
    const auto config_path = get_temp_config_path("labels_and_blur.ini");
    write_config_file(config_path,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[ROI]
labels= person,car , bike
pack_rois=true
background_mode=background_blur
)ini");

    const auto config = load_encoder_config(config_path);

    ASSERT_EQ(config.labels.size(), 3U);
    EXPECT_EQ(config.labels[0], "person");
    EXPECT_EQ(config.labels[1], "car");
    EXPECT_EQ(config.labels[2], "bike");
    EXPECT_EQ(config.background_mode_value, background_mode::background_blur);
    EXPECT_EQ(config.effective_background_mode_value, background_mode::background_blur);
    EXPECT_FALSE(config.pack_rois);
}

TEST(EncoderConfigLoader, MissingBackgroundModeKeepsBackgroundProcessingOff) {
    const auto config_path = get_temp_config_path("background_mode_default_off.ini");
    write_config_file(config_path,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[ROI]
pack_rois=true
)ini");

    const auto config = load_encoder_config(config_path);
    EXPECT_EQ(config.background_mode_value, background_mode::off);
    EXPECT_EQ(config.effective_background_mode_value, background_mode::off);
    EXPECT_TRUE(config.pack_rois);
}

TEST(EncoderConfigLoader, RejectsOutOfRangeEncoderQp) {
    const auto config_path = get_temp_config_path("invalid_qp.ini");
    write_config_file(config_path,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[Encoder]
qp=52
)ini");

    EXPECT_THROW(static_cast<void>(load_encoder_config(config_path)), std::runtime_error);
}

TEST(EncoderConfigLoader, AcceptsEncoderQpBoundaryValues) {
    const auto config_path_zero = get_temp_config_path("qp_zero.ini");
    write_config_file(config_path_zero,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[Encoder]
qp=0
)ini");
    const auto config_zero = load_encoder_config(config_path_zero);
    ASSERT_TRUE(config_zero.encoder_qp.has_value());
    EXPECT_EQ(*config_zero.encoder_qp, 0);

    const auto config_path_max = get_temp_config_path("qp_max.ini");
    write_config_file(config_path_max,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[Encoder]
qp=51
)ini");
    const auto config_max = load_encoder_config(config_path_max);
    ASSERT_TRUE(config_max.encoder_qp.has_value());
    EXPECT_EQ(*config_max.encoder_qp, 51);
}

TEST(EncoderConfigLoader, RejectsInvalidCommunicationTransportType) {
    const auto config_path = get_temp_config_path("invalid_transport.ini");
    write_config_file(config_path,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[Communication]
transport_type=invalid
host=127.0.0.1
port=5000
)ini");

    EXPECT_THROW(static_cast<void>(load_encoder_config(config_path)), std::runtime_error);
}

TEST(EncoderConfigLoader, ParsesLumaAdaptationFieldsFromRoiSection) {
    const auto config_path = get_temp_config_path("luma_adaptation.ini");
    write_config_file(config_path,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[ROI]
enable_luma_adaptation=true
luma_downscale_factor=0.65
)ini");

    const auto config = load_encoder_config(config_path);
    EXPECT_TRUE(config.enable_luma_adaptation);
    EXPECT_DOUBLE_EQ(config.luma_downscale_factor, 0.65);
}

TEST(EncoderConfigLoader, ParsesAdaptiveLumaFieldsFromRoiSection) {
    const auto config_path = get_temp_config_path("adaptive_luma.ini");
    write_config_file(config_path,
                      R"ini([Common]
input_type=file
width=640
height=480
framerate=30

[Video]
file_name=dummy_640x480_30.yuv
format=I420

[ROI]
enable_luma_adaptation=true
luma_downscale_factor=0.6
enable_adaptive_luma_adaptation=true
input_analyzer_model_path=yolo/model_weights.onnx
adaptive_luma_analyzer_on_keyframes_only=false
)ini");

    const auto config = load_encoder_config(config_path);
    EXPECT_TRUE(config.enable_luma_adaptation);
    EXPECT_DOUBLE_EQ(config.luma_downscale_factor, 0.6);
    EXPECT_TRUE(config.enable_adaptive_luma_adaptation);
    EXPECT_EQ(config.input_analyzer_model_path, "yolo/model_weights.onnx");
    EXPECT_FALSE(config.adaptive_luma_analyzer_on_keyframes_only);
}
