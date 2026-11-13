// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include <vector>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/common_input.h"
#include "../../encoder_cli/src/gst/input_types/input_ingress_facade.h"
#include "../../encoder_cli/src/gst/output_pipeline/paths.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 2;
        config.height = 2;
        config.framerate = 30;
        config.format = sf::video_format::i420;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_2x2_30.yuv"));
        return config;
    }
} // namespace

class CommonInputTest : public GStreamerTestFixture {};

TEST_F(CommonInputTest, DetermineModelNameAlwaysReturnsYolo) {
    global_data data(make_base_config());
    EXPECT_EQ(determine_model_name(), "yolo");
}

TEST_F(CommonInputTest, ConfigureAppsinkRejectsNullElement) {
    global_data data(make_base_config());
    input_ingress_facade view(data);
    EXPECT_FALSE(configure_appsink(nullptr, view));
}

TEST_F(CommonInputTest, CreateInputSinkBranchElementsSkipsOriginalBranchWhenSaveFramesDisabled) {
    auto config = make_base_config();
    config.debug.save_frames = false;
    global_data data(std::move(config));
    input_ingress_facade view(data);

    input_sink_branch_elements sink_elements{};
    ASSERT_TRUE(
            create_input_sink_branch_elements(view, "test-common-input-appsink", "common-input-test", sink_elements));
    ASSERT_NE(sink_elements.appsink, nullptr);
    EXPECT_EQ(sink_elements.queue_original, nullptr);
    EXPECT_EQ(sink_elements.converter_original, nullptr);
    EXPECT_EQ(sink_elements.encoder_original, nullptr);
    EXPECT_EQ(sink_elements.sink_original, nullptr);

    gst_object_unref(sink_elements.appsink);
}

TEST_F(CommonInputTest, CreateCommonInputElementsComputesExpectedFrameCountForRawFile) {
    const std::filesystem::path test_file_path = "common_input_test_2x2_i420.yuv";
    {
        std::ofstream file_stream(test_file_path, std::ios::binary);
        ASSERT_TRUE(file_stream.is_open());
        // I420 frame size for 2x2 is 2 * 2 * 3 / 2 = 6 bytes. Write 3 frames.
        const std::vector<char> payload(18, '\x2a');
        file_stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    auto config = make_base_config();
    config.file_name = sf::unique_g_char_ptr(g_strdup(test_file_path.c_str()));
    global_data data(std::move(config));
    data.expected_file_input_frames = 99;
    data.file_input_forced_raw_eos = true;

    GstElement *filesrc = nullptr;
    GstElement *parser = nullptr;
    GstElement *tee = nullptr;
    input_ingress_facade view(data);
    ASSERT_TRUE(create_common_input_elements(view, filesrc, parser, tee));
    ASSERT_NE(filesrc, nullptr);
    ASSERT_NE(parser, nullptr);
    ASSERT_NE(tee, nullptr);
    ASSERT_TRUE(data.expected_file_input_frames.has_value());
    EXPECT_EQ(*data.expected_file_input_frames, static_cast<guint64>(3));
    EXPECT_FALSE(data.file_input_forced_raw_eos.load());

    gst_object_unref(filesrc);
    gst_object_unref(parser);
    gst_object_unref(tee);
    std::filesystem::remove(test_file_path);
}

TEST_F(CommonInputTest, LinkOriginalFrameSavingBranchReturnsTrueWhenElementsNotProvided) {
    EXPECT_TRUE(link_original_frame_saving_branch(nullptr, nullptr, nullptr, nullptr, nullptr));
}

TEST_F(CommonInputTest, LinkOriginalFrameSavingBranchFailsWhenTeeIsMissing) {
    GstElement *queue_original = gst_element_factory_make("queue", "test-common-queue");
    GstElement *converter_original = gst_element_factory_make("videoconvert", "test-common-convert");
    GstElement *encoder_original = gst_element_factory_make("pngenc", "test-common-encoder");
    GstElement *sink_original = gst_element_factory_make("fakesink", "test-common-sink");
    ASSERT_NE(queue_original, nullptr);
    ASSERT_NE(converter_original, nullptr);
    ASSERT_NE(encoder_original, nullptr);
    ASSERT_NE(sink_original, nullptr);

    EXPECT_FALSE(link_original_frame_saving_branch(nullptr, queue_original, converter_original, encoder_original,
                                                   sink_original));

    gst_object_unref(queue_original);
    gst_object_unref(converter_original);
    gst_object_unref(encoder_original);
    gst_object_unref(sink_original);
}
