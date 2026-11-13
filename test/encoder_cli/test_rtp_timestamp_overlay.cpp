// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/rtp_timestamp_overlay.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }
} // namespace

class RtpTimestampOverlayTest : public GStreamerTestFixture {};

TEST_F(RtpTimestampOverlayTest, CreateTimestampRoiRectangleUsesExpectedPlacement) {
    const auto rectangle = create_timestamp_roi_rectangle(640, 480);

    EXPECT_EQ(rectangle.left, 10);
    EXPECT_EQ(rectangle.top, 458);
    EXPECT_EQ(rectangle.width, 107);
    EXPECT_EQ(rectangle.height, 12);
}

TEST_F(RtpTimestampOverlayTest, CreateTimestampRoiRectangleClampsToSmallFrame) {
    const auto rectangle = create_timestamp_roi_rectangle(50, 30);

    EXPECT_EQ(rectangle.left, 10);
    EXPECT_EQ(rectangle.top, 8);
    EXPECT_EQ(rectangle.width, 40);
    EXPECT_EQ(rectangle.height, 12);
}

TEST_F(RtpTimestampOverlayTest, SetupFunctionsRejectNullElements) {
    EXPECT_FALSE(setup_rtp_timestamp_extraction(nullptr, 90000U));
    EXPECT_FALSE(setup_rtp_timestamp_attachment(nullptr));

    auto config = make_base_config();
    config.debug.draw_rtp_timestamp = true;
    global_data data(std::move(config));
    EXPECT_FALSE(setup_rtp_timestamp_overlay(nullptr, data.params));
}

TEST_F(RtpTimestampOverlayTest, SetupOverlayIsNoopWhenTimestampDrawingDisabled) {
    auto config = make_base_config();
    config.debug.draw_rtp_timestamp = false;
    global_data data(std::move(config));

    EXPECT_TRUE(setup_rtp_timestamp_overlay(nullptr, data.params));
}

TEST_F(RtpTimestampOverlayTest, DrawTimestampGuardsInvalidInputs) {
    frame_properties properties{};
    properties.width = 640;
    properties.height = 480;
    properties.format = "I420";

    EXPECT_FALSE(draw_rtp_timestamp_on_buffer(nullptr, properties));

    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 640 * 480 * 3 / 2, nullptr);
    ASSERT_NE(buffer, nullptr);

    frame_properties missing_format{};
    missing_format.width = 640;
    missing_format.height = 480;
    missing_format.format = nullptr;
    EXPECT_FALSE(draw_rtp_timestamp_on_buffer(buffer, missing_format));

    GST_BUFFER_PTS(buffer) = GST_CLOCK_TIME_NONE;
    EXPECT_FALSE(draw_rtp_timestamp_on_buffer(buffer, properties));

    gst_buffer_unref(buffer);
}

TEST_F(RtpTimestampOverlayTest, ClearTimestampStorageIsIdempotent) {
    clear_rtp_timestamps();
    clear_rtp_timestamps();
    SUCCEED();
}
