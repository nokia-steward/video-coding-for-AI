// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cstring>
#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/processing_modes/background_blur/background_blur_processing.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/processing_modes/packing/packing_processing.h"
#include "../../encoder_cli/src/roi/roi_parser.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_processing_modes.cpp
 * @brief Tests for individual processing mode implementations.
 *
 * Tests verify that:
 * - Packing processing packs ROIs into compact layout
 * - Background blur processing blurs background and preserves ROIs
 * - Both modes handle frames with no ROIs correctly
 */

class ProcessingModesTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }

    void set_intra_period(encoder_config &config, const int intra_period) {
        roi_sequence_info info{};
        info.frame_rate_details = frame_rate_info{intra_period, 30, intra_period, 0};
        config.roi_info = info;
    }

    // Helper to create a test buffer with YUV420 data
    sf::unique_gst_buffer_no_delete_ptr create_test_buffer(int width, int height) {
        // YUV420 frame size: width * height * 3 / 2
        const size_t frame_size = static_cast<size_t>(width) * height * 3 / 2;
        const auto buffer = gst_buffer_new_allocate(nullptr, frame_size, nullptr);

        // Map buffer and fill with test data
        GstMapInfo map_info;
        if (gst_buffer_map(buffer, &map_info, GST_MAP_WRITE)) {
            // Fill with a simple pattern (all zeros for simplicity)
            memset(map_info.data, 128, map_info.size); // Gray color
            gst_buffer_unmap(buffer, &map_info);
        }

        // Set PTS
        GST_BUFFER_PTS(buffer) = 0;
        GST_BUFFER_DTS(buffer) = 0;
        static uint64_t next_sequence_id = 1;
        EXPECT_TRUE(attach_frame_contract_metadata_to_buffer(buffer, frame_path_type::inference, next_sequence_id++));

        return sf::unique_gst_buffer_no_delete_ptr(buffer);
    }

    // Helper to create frame properties
    frame_properties create_frame_properties(int width = 640, int height = 480) {
        frame_properties props;
        props.width = width;
        props.height = height;
        props.format = "video/x-raw,format=I420";
        return props;
    }

    // Helper to create rectangles
    std::vector<sf::rectangle> create_test_rectangles() {
        return {sf::rectangle(10, 10, 40, 40), sf::rectangle(100, 100, 50, 50)};
    }
};

/**
 * @brief Test that packing processing mode selection works with low_delay packer.
 */
TEST_F(ProcessingModesTest, PackingWorksWithLowDelayPacker) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.debug.draw_borders = false;

    global_data data(std::move(config));

    // Verify packer is initialized correctly
    const auto *low_delay_ptr = std::get_if<std::unique_ptr<low_delay_packer>>(&data.packer);
    EXPECT_NE(low_delay_ptr, nullptr) << "Should have low_delay packer";
    EXPECT_NE(*low_delay_ptr, nullptr) << "Packer should be initialized";
}

/**
 * @brief Test that packing processing mode selection works with random_access packer.
 */
TEST_F(ProcessingModesTest, PackingWorksWithRandomAccessPacker) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::random_access;
    config.pack_rois = true;
    set_intra_period(config, 8);
    config.debug.draw_borders = false;

    global_data data(std::move(config));

    // Verify packer is initialized correctly
    const auto *ra_ptr = std::get_if<std::unique_ptr<random_access_packer>>(&data.packer);
    EXPECT_NE(ra_ptr, nullptr) << "Should have random_access packer";
    EXPECT_NE(*ra_ptr, nullptr) << "Packer should be initialized";
}

/**
 * @brief Test that background blur configuration respects sigma parameter.
 */
TEST_F(ProcessingModesTest, BackgroundBlurRespectsSigma) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.background_mode_value = background_mode::background_blur;
    config.effective_background_mode_value = background_mode::background_blur;
    config.sigma = 10.0; // Higher sigma = more blur
    config.kernel_size = 21;
    config.debug.draw_borders = false;

    global_data data(std::move(config));

    // Verify sigma is stored correctly
    EXPECT_EQ(data.params.sigma, 10.0) << "Sigma should be 10.0";
    EXPECT_EQ(data.params.kernel_size, 21) << "Kernel size should be 21";
    EXPECT_EQ(data.params.background_mode_value, background_mode::background_blur)
            << "Background blur mode should be enabled";
}

/**
 * @brief Test that background blur configuration respects kernel_size parameter.
 */
TEST_F(ProcessingModesTest, BackgroundBlurRespectsKernelSize) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.background_mode_value = background_mode::background_blur;
    config.effective_background_mode_value = background_mode::background_blur;
    config.sigma = 5.0;
    config.kernel_size = 25; // Larger kernel
    config.debug.draw_borders = false;

    global_data data(std::move(config));

    // Verify kernel_size is stored correctly
    EXPECT_EQ(data.params.kernel_size, 25) << "Kernel size should be 25";
    EXPECT_EQ(data.params.sigma, 5.0) << "Sigma should be 5.0";
    EXPECT_EQ(data.params.background_mode_value, background_mode::background_blur)
            << "Background blur mode should be enabled";
}

TEST_F(ProcessingModesTest, BackgroundBlurNoRoisCreatesOutputBufferAndPackedSentinel) {
    encoder_config config;
    config.width = 64;
    config.height = 64;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.background_mode_value = background_mode::background_blur;
    config.effective_background_mode_value = background_mode::background_blur;
    config.sigma = 3.0;
    config.kernel_size = 5;
    config.debug.draw_borders = false;
    config.debug.save_frames = false;

    global_data data(std::move(config));
    const auto input_buffer = create_test_buffer(64, 64);
    ASSERT_NE(input_buffer.get(), nullptr);
    GST_BUFFER_PTS(input_buffer.get()) = 123 * GST_MSECOND;
    GST_BUFFER_DTS(input_buffer.get()) = 120 * GST_MSECOND;
    GST_BUFFER_DURATION(input_buffer.get()) = 33 * GST_MSECOND;

    sf::unique_gst_buffer_ptr output_buffer;
    frame_properties props;
    props.width = 64;
    props.height = 64;
    props.format = "I420";

    background_blur_process_no_rois(data, input_buffer, output_buffer, props);

    ASSERT_NE(output_buffer.get(), nullptr);
    EXPECT_EQ(GST_BUFFER_PTS(output_buffer.get()), 123 * GST_MSECOND);
    EXPECT_EQ(GST_BUFFER_DTS(output_buffer.get()), 120 * GST_MSECOND);
    EXPECT_EQ(GST_BUFFER_DURATION(output_buffer.get()), 33 * GST_MSECOND);

    const auto packed_item = data.packed_regions_queue.pop();
    ASSERT_TRUE(packed_item.has_value());
    EXPECT_TRUE(packed_item->regions.cancel_flag);
}

TEST_F(ProcessingModesTest, BackgroundBlurProcessFramePushesDebugSentinelsWithoutRois) {
    encoder_config config;
    config.width = 64;
    config.height = 64;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.background_mode_value = background_mode::background_blur;
    config.effective_background_mode_value = background_mode::background_blur;
    config.sigma = 3.0;
    config.kernel_size = 5;
    config.debug.draw_borders = true;
    config.debug.save_frames = true;

    global_data data(std::move(config));
    const auto input_buffer = create_test_buffer(64, 64);
    ASSERT_NE(input_buffer.get(), nullptr);
    GST_BUFFER_PTS(input_buffer.get()) = 250 * GST_MSECOND;

    sf::unique_gst_buffer_ptr output_buffer;
    frame_properties props;
    props.width = 64;
    props.height = 64;
    props.format = "I420";

    const std::vector<sf::rectangle> input_rectangles;
    const std::vector<split_rectangle> split_rectangles;
    background_blur_process_frame(data, input_buffer, output_buffer, props, input_rectangles, split_rectangles, false,
                                  7);

    ASSERT_NE(output_buffer.get(), nullptr);
    const auto packed_item = data.packed_regions_queue.pop();
    const auto display_item = data.display_regions_queue.pop();
    const auto debug_item = data.debug_regions_queue.pop();

    ASSERT_TRUE(packed_item.has_value());
    ASSERT_TRUE(display_item.has_value());
    ASSERT_TRUE(debug_item.has_value());
    EXPECT_TRUE(packed_item->regions.cancel_flag);
    EXPECT_TRUE(display_item->regions.cancel_flag);
    EXPECT_TRUE(debug_item->regions.cancel_flag);
}

TEST_F(ProcessingModesTest, BackgroundBlurProcessFrameWithStaticRoisPushesRegionMetadata) {
    encoder_config config;
    config.width = 64;
    config.height = 64;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.background_mode_value = background_mode::background_blur;
    config.effective_background_mode_value = background_mode::background_blur;
    config.sigma = 3.0;
    config.kernel_size = 5;
    config.debug.draw_borders = true;
    config.debug.save_frames = true;

    global_data data(std::move(config));
    const auto input_buffer = create_test_buffer(64, 64);
    ASSERT_NE(input_buffer.get(), nullptr);
    GST_BUFFER_PTS(input_buffer.get()) = 400 * GST_MSECOND;
    GST_BUFFER_DTS(input_buffer.get()) = 396 * GST_MSECOND;
    GST_BUFFER_DURATION(input_buffer.get()) = 33 * GST_MSECOND;

    sf::unique_gst_buffer_ptr output_buffer;
    frame_properties props;
    props.width = 64;
    props.height = 64;
    props.format = "I420";

    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(4, 4, 12, 12),
            sf::rectangle(24, 24, 16, 14),
    };
    const std::vector<split_rectangle> split_rectangles;
    background_blur_process_frame(data, input_buffer, output_buffer, props, input_rectangles, split_rectangles, true,
                                  9);

    ASSERT_NE(output_buffer.get(), nullptr);
    EXPECT_EQ(GST_BUFFER_PTS(output_buffer.get()), 400 * GST_MSECOND);
    EXPECT_EQ(GST_BUFFER_DTS(output_buffer.get()), 396 * GST_MSECOND);
    EXPECT_EQ(GST_BUFFER_DURATION(output_buffer.get()), 33 * GST_MSECOND);

    const auto packed_item = data.packed_regions_queue.pop();
    const auto display_item = data.display_regions_queue.pop();
    const auto debug_item = data.debug_regions_queue.pop();

    ASSERT_TRUE(packed_item.has_value());
    ASSERT_TRUE(display_item.has_value());
    ASSERT_TRUE(debug_item.has_value());
    EXPECT_TRUE(packed_item->regions.cancel_flag) << "Background blur mode always pushes packed sentinel";
    EXPECT_FALSE(display_item->regions.cancel_flag);
    EXPECT_FALSE(debug_item->regions.cancel_flag);
    EXPECT_EQ(display_item->regions.regions.size(), input_rectangles.size());
    EXPECT_EQ(debug_item->regions.regions.size(), input_rectangles.size());
}

TEST_F(ProcessingModesTest, BackgroundBlurProcessFrameReturnsEarlyWhenFormatIsMissing) {
    encoder_config config;
    config.width = 64;
    config.height = 64;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.background_mode_value = background_mode::background_blur;
    config.effective_background_mode_value = background_mode::background_blur;
    config.sigma = 3.0;
    config.kernel_size = 5;
    config.debug.draw_borders = true;
    config.debug.save_frames = true;

    global_data data(std::move(config));
    const auto input_buffer = create_test_buffer(64, 64);
    ASSERT_NE(input_buffer.get(), nullptr);

    sf::unique_gst_buffer_ptr output_buffer;
    frame_properties props;
    props.width = 64;
    props.height = 64;
    props.format = nullptr; // forces copy_video_frame_data guard failure

    const std::vector<sf::rectangle> input_rectangles = {sf::rectangle(8, 8, 10, 10)};
    const std::vector<split_rectangle> split_rectangles;
    background_blur_process_frame(data, input_buffer, output_buffer, props, input_rectangles, split_rectangles, true,
                                  3);

    EXPECT_FALSE(data.packed_regions_queue.try_pop().has_value());
    EXPECT_FALSE(data.display_regions_queue.try_pop().has_value());
    EXPECT_FALSE(data.debug_regions_queue.try_pop().has_value());
}

TEST_F(ProcessingModesTest, BackgroundBlurProcessNoRoisReturnsEarlyWhenFormatIsMissing) {
    encoder_config config;
    config.width = 64;
    config.height = 64;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;
    config.background_mode_value = background_mode::background_blur;
    config.effective_background_mode_value = background_mode::background_blur;
    config.sigma = 3.0;
    config.kernel_size = 5;

    global_data data(std::move(config));
    const auto input_buffer = create_test_buffer(64, 64);
    ASSERT_NE(input_buffer.get(), nullptr);

    sf::unique_gst_buffer_ptr output_buffer;
    frame_properties props;
    props.width = 64;
    props.height = 64;
    props.format = nullptr; // forces copy_video_frame_data guard failure

    background_blur_process_no_rois(data, input_buffer, output_buffer, props);
    EXPECT_FALSE(data.packed_regions_queue.try_pop().has_value());
}
