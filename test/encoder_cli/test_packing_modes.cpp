// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/roi_detection_packing.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/packing_modes/low_delay/low_delay_packing.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/packing_modes/random_access/random_access_packing.h"
#include "../../encoder_cli/src/roi/pri_packer_view.h"
#include "../../encoder_cli/src/roi/roi_parser.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_packing_modes.cpp
 * @brief Tests for individual packing mode implementations.
 *
 * Tests verify that:
 * - Low delay packing processes frames immediately
 * - Random access packing buffers frames until period completion
 * - Frame accumulation and period boundaries work correctly
 */

class PackingModesTest : public GStreamerTestFixture {
protected:
    void SetUp() override { GStreamerTestFixture::SetUp(); }

    void set_intra_period(encoder_config &config, const int intra_period) {
        roi_sequence_info info{};
        info.frame_rate_details = frame_rate_info{intra_period, 30, intra_period, 0};
        config.roi_info = info;
    }

    // Helper to create a dummy sample
    sf::unique_gst_sample_ptr create_dummy_sample() {
        const auto buffer = gst_buffer_new_allocate(nullptr, 100, nullptr);
        const auto caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", "width", G_TYPE_INT, 640,
                                              "height", G_TYPE_INT, 480, nullptr);
        const auto sample = gst_sample_new(buffer, caps, nullptr, nullptr);
        gst_buffer_unref(buffer);
        gst_caps_unref(caps);
        return sf::unique_gst_sample_ptr(sample);
    }

    // Helper to create rectangles
    std::vector<sf::rectangle> create_test_rectangles() {
        return {sf::rectangle(10, 10, 40, 40), sf::rectangle(100, 100, 50, 50)};
    }
};

/**
 * @brief Test that low_delay_process_frame returns false.
 */
TEST_F(PackingModesTest, LowDelayProcessFrameReturnsFalse) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    auto sample = create_dummy_sample();
    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 0;

    const bool result = low_delay_process_frame(packer, packing_cfg, sample, input_rectangles, split_rectangles,
                                                is_static, frame_number);

    EXPECT_FALSE(result) << "Low delay should return false from process_frame";
}

/**
 * @brief Test that low_delay_get_packed_rectangles returns packing result.
 */
TEST_F(PackingModesTest, LowDelayGetPackedRectanglesReturnsResult) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 0;

    const auto result = low_delay_get_packed_rectangles(packer, packing_cfg, input_rectangles, split_rectangles,
                                                        is_static, frame_number);

    ASSERT_TRUE(result.has_value()) << "Low delay should return packing result";
    EXPECT_FALSE(result->packed_rectangles.empty()) << "Should have packed rectangles";
}

/**
 * @brief Test that low_delay processes frames immediately (no buffering).
 */
TEST_F(PackingModesTest, LowDelayProcessesImmediately) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 1;

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;

    // Process multiple frames - each should return immediately
    for (int frame_number = 0; frame_number < 5; ++frame_number) {
        const auto result = low_delay_get_packed_rectangles(packer, packing_cfg, input_rectangles, split_rectangles,
                                                            is_static, frame_number);

        ASSERT_TRUE(result.has_value()) << "Frame " << frame_number << " should return result immediately";
        EXPECT_FALSE(result->packed_rectangles.empty()) << "Frame " << frame_number << " should have packed rectangles";
    }
}

/**
 * @brief Test that low_delay respects num_frames parameter.
 */
TEST_F(PackingModesTest, LowDelayRespectsNumFrames) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay;
    config.pack_rois = true;
    config.num_frames = 3; // Accumulate 3 frames

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;

    // Process frames - packer should accumulate
    for (int frame_number = 0; frame_number < 5; ++frame_number) {
        const auto result = low_delay_get_packed_rectangles(packer, packing_cfg, input_rectangles, split_rectangles,
                                                            is_static, frame_number);

        ASSERT_TRUE(result.has_value()) << "Frame " << frame_number << " should return result";
    }
}

/**
 * @brief Test that random_access_process_frame returns true when packer exists.
 */
TEST_F(PackingModesTest, RandomAccessProcessFrameReturnsTrue) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::random_access;
    config.pack_rois = true;
    set_intra_period(config, 8);

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    auto sample = create_dummy_sample();
    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 0;

    const bool result = random_access_process_frame(packer, packing_cfg, sample, input_rectangles, split_rectangles,
                                                    is_static, frame_number);

    EXPECT_TRUE(result) << "Random access should return true when packer exists";
    // Sample should be moved (nullptr after call)
    EXPECT_EQ(sample.get(), nullptr) << "Sample should be moved to packer";
}

/**
 * @brief Test that random_access_process_frame returns false when packer doesn't exist.
 */
TEST_F(PackingModesTest, RandomAccessProcessFrameReturnsFalseWhenNoPacker) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::low_delay; // Wrong packer type
    config.pack_rois = true;

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    auto sample = create_dummy_sample();
    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 0;

    const bool result = random_access_process_frame(packer, packing_cfg, sample, input_rectangles, split_rectangles,
                                                    is_static, frame_number);

    EXPECT_FALSE(result) << "Random access should return false when packer type doesn't match";
    EXPECT_NE(sample.get(), nullptr) << "Sample should remain owned by caller when frame is not handled";
}

TEST_F(PackingModesTest, RandomAccessProcessFrameReturnsFalseWhenRandomAccessPackerPointerIsNull) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::random_access;
    config.pack_rois = true;
    set_intra_period(config, 8);

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);
    data.packer = std::unique_ptr<random_access_packer>{};

    auto sample = create_dummy_sample();
    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 0;

    const bool result = random_access_process_frame(packer, packing_cfg, sample, input_rectangles, split_rectangles,
                                                    is_static, frame_number);

    EXPECT_FALSE(result) << "Random access should return false when packer alternative exists but pointer is null";
    EXPECT_NE(sample.get(), nullptr) << "Sample should remain owned by caller when frame is not handled";
}

/**
 * @brief Test that random_access_get_packed_rectangles returns nullopt.
 */
TEST_F(PackingModesTest, RandomAccessGetPackedRectanglesReturnsNullopt) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::random_access;
    config.pack_rois = true;
    set_intra_period(config, 8);

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 0;

    const auto result = random_access_get_packed_rectangles(packer, packing_cfg, input_rectangles, split_rectangles,
                                                            is_static, frame_number);

    EXPECT_FALSE(result.has_value()) << "Random access should return nullopt from get_packed_rectangles";
}

/**
 * @brief Test that random_access buffers frames (process_frame doesn't return packed result).
 */
TEST_F(PackingModesTest, RandomAccessBuffersFrames) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::random_access;
    config.pack_rois = true;
    set_intra_period(config, 4); // Period of 4 frames

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;

    // Process frames - they should be buffered, not returned immediately
    for (int frame_number = 0; frame_number < 3; ++frame_number) {
        auto sample = create_dummy_sample();
        const bool result = random_access_process_frame(packer, packing_cfg, sample, input_rectangles, split_rectangles,
                                                        is_static, frame_number);

        EXPECT_TRUE(result) << "Frame " << frame_number << " should be accepted by packer";
        // get_packed_rectangles should return nullopt (not used in random_access)
        const auto packed_result = random_access_get_packed_rectangles(packer, packing_cfg, input_rectangles,
                                                                       split_rectangles, is_static, frame_number);
        EXPECT_FALSE(packed_result.has_value())
                << "Random access should not return packed result from get_packed_rectangles";
    }
}

TEST_F(PackingModesTest, RandomAccessProcessFrameHandlesNullSampleSafely) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::random_access;
    config.pack_rois = true;
    set_intra_period(config, 8);

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    sf::unique_gst_sample_ptr null_sample;
    const auto input_rectangles = create_test_rectangles();
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 0;

    const bool result = random_access_process_frame(packer, packing_cfg, null_sample, input_rectangles,
                                                    split_rectangles, is_static, frame_number);

    EXPECT_TRUE(result) << "Null sample should be treated as handled to avoid downstream crashes";
    EXPECT_EQ(null_sample.get(), nullptr);
}

TEST_F(PackingModesTest, RandomAccessProcessFrameAcceptsEmptyRectanglesAndMovesSample) {
    encoder_config config;
    config.width = 640;
    config.height = 480;
    config.packer_type_value = packer_type::random_access;
    config.pack_rois = true;
    set_intra_period(config, 8);

    global_data data(std::move(config));
    pri_packer_view packer(data.packer);
    const auto packing_cfg = make_packing_config(data);

    auto sample = create_dummy_sample();
    const std::vector<sf::rectangle> input_rectangles;
    const std::vector<split_rectangle> split_rectangles;
    const bool is_static = false;
    const int frame_number = 1;

    const bool result = random_access_process_frame(packer, packing_cfg, sample, input_rectangles, split_rectangles,
                                                    is_static, frame_number);

    EXPECT_TRUE(result);
    EXPECT_EQ(sample.get(), nullptr) << "Sample should be moved into the random-access packer";
}
