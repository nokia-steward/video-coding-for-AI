// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/rtp_timestamp_overlay.h"
#include "../../encoder_cli/src/processing/roi_detection_packing.h"
#include "../../encoder_cli/src/roi/pri_packer_view.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config create_base_config() {
        encoder_config config;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.input_type_value = input_type::file;
        config.pack_rois = true;
        config.background_mode_value = background_mode::off;
        config.effective_background_mode_value = background_mode::off;
        config.packer_type_value = packer_type::low_delay;
        config.num_frames = 1;
        return config;
    }

    [[nodiscard]] std::vector<split_rectangle>
    create_split_rectangles_for(const std::vector<sf::rectangle> &rectangles) {
        std::vector<split_rectangle> split_rectangles;
        split_rectangles.reserve(rectangles.size());
        for (size_t index = 0; index < rectangles.size(); ++index) {
            split_rectangles.emplace_back(rectangles[index], static_cast<uint32_t>(100 + index), 0, sf::yuv_color::blue,
                                          0.95F, false);
        }
        return split_rectangles;
    }

    void assert_result_is_bounded_and_valid(const packing_result &result, const int width, const int height) {
        for (const auto &rect: result.packed_rectangles) {
            ASSERT_TRUE(rect.is_valid());
            EXPECT_GE(rect.left, 0);
            EXPECT_GE(rect.top, 0);
            EXPECT_LE(rect.left + rect.width, width);
            EXPECT_LE(rect.top + rect.height, height);
        }

        for (const auto packed_index: result.original_to_packed_mapping) {
            EXPECT_LT(packed_index, result.packed_rectangles.size());
        }
    }
} // namespace

class RoiDetectionPackingTest : public GStreamerTestFixture {};

TEST_F(RoiDetectionPackingTest, ContentMarginExpandsSourceAndPackedRectangles) {
    auto config = create_base_config();
    config.roi_fixed_margin_px = 20;
    config.roi_margin_mode_value = roi_margin_mode::content;

    global_data data(std::move(config));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);

    const std::vector<sf::rectangle> input_rectangles = {sf::rectangle(10, 10, 20, 20)};
    const auto split_rectangles = create_split_rectangles_for(input_rectangles);

    const auto result = perform_detection_packing(data_view, data_cfg, input_rectangles, split_rectangles, false, 0);

    ASSERT_EQ(result.original_rois.size(), 1U);
    ASSERT_EQ(result.packed_rectangles.size(), 1U);
    EXPECT_EQ(result.original_to_packed_mapping.size(), 1U);
    EXPECT_LT(result.original_to_packed_mapping[0], result.packed_rectangles.size());

    // Expanded ROI should be clamped to frame borders.
    EXPECT_EQ(result.original_rois[0], sf::rectangle(0, 0, 50, 50));
    EXPECT_EQ(result.packed_rectangles[0].width, 50);
    EXPECT_EQ(result.packed_rectangles[0].height, 50);
}

TEST_F(RoiDetectionPackingTest, PlacementMarginKeepsSourceUndilated) {
    auto config = create_base_config();
    config.roi_fixed_margin_px = 20;
    config.roi_margin_mode_value = roi_margin_mode::placement;

    global_data data(std::move(config));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);

    const std::vector<sf::rectangle> input_rectangles = {sf::rectangle(10, 10, 20, 20)};
    const auto split_rectangles = create_split_rectangles_for(input_rectangles);

    const auto result = perform_detection_packing(data_view, data_cfg, input_rectangles, split_rectangles, false, 0);

    ASSERT_EQ(result.original_rois.size(), 1U);
    ASSERT_EQ(result.packed_rectangles.size(), 1U);
    EXPECT_EQ(result.original_rois[0], input_rectangles[0]);
    EXPECT_EQ(result.packed_rectangles[0].width, input_rectangles[0].width);
    EXPECT_EQ(result.packed_rectangles[0].height, input_rectangles[0].height);
}

TEST_F(RoiDetectionPackingTest, MergeMappingUsesValidPackedIndices) {
    auto config = create_base_config();
    config.roi_margin_mode_value = roi_margin_mode::off;

    global_data data(std::move(config));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);

    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(10, 10, 40, 40),
            sf::rectangle(20, 20, 40, 40),
    };
    const auto split_rectangles = create_split_rectangles_for(input_rectangles);

    const auto result = perform_detection_packing(data_view, data_cfg, input_rectangles, split_rectangles, false, 0);

    ASSERT_EQ(result.original_rois.size(), 2U);
    ASSERT_EQ(result.packed_rectangles.size(), 2U);
    ASSERT_EQ(result.original_to_packed_mapping.size(), 2U);

    for (const size_t packed_index: result.original_to_packed_mapping) {
        EXPECT_LT(packed_index, result.packed_rectangles.size());
    }

    // Both originals should map to the same packed slot after merge.
    EXPECT_EQ(result.original_to_packed_mapping[0], result.original_to_packed_mapping[1]);
}

TEST_F(RoiDetectionPackingTest, PackingFailureFallsBackToOriginalPositions) {
    auto config = create_base_config();
    config.width = 64;
    config.height = 64;
    config.roi_margin_mode_value = roi_margin_mode::off;

    global_data data(std::move(config));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);

    const std::vector<sf::rectangle> input_rectangles = {sf::rectangle(0, 0, 128, 128)};
    const auto split_rectangles = create_split_rectangles_for(input_rectangles);

    const auto result = perform_detection_packing(data_view, data_cfg, input_rectangles, split_rectangles, false, 0);

    ASSERT_EQ(result.original_rois.size(), 1U);
    ASSERT_EQ(result.packed_rectangles.size(), 1U);
    ASSERT_EQ(result.original_to_packed_mapping.size(), 1U);
    EXPECT_EQ(result.original_to_packed_mapping[0], 0U);
    EXPECT_EQ(result.original_rois[0], input_rectangles[0]);
    EXPECT_EQ(result.packed_rectangles[0], input_rectangles[0]);
}

TEST_F(RoiDetectionPackingTest, EmptyInputAppendsTimestampRoiWhenEnabled) {
    auto config = create_base_config();
    config.debug.draw_rtp_timestamp = true;

    global_data data(std::move(config));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);

    const std::vector<sf::rectangle> input_rectangles;
    const std::vector<split_rectangle> split_rectangles;

    const auto result = perform_detection_packing(data_view, data_cfg, input_rectangles, split_rectangles, false, 0);
    const sf::rectangle expected_timestamp = create_timestamp_roi_rectangle(data.params.width, data.params.height);

    ASSERT_EQ(result.original_rois.size(), 1U);
    ASSERT_EQ(result.packed_rectangles.size(), 1U);
    EXPECT_TRUE(result.original_to_packed_mapping.empty());
    EXPECT_EQ(result.original_rois[0], expected_timestamp);
    EXPECT_EQ(result.packed_rectangles[0], expected_timestamp);
}

TEST_F(RoiDetectionPackingTest, DuplicateBoundsWithSplitMismatchKeepsMappingBoundedAcrossFrames) {
    auto config = create_base_config();
    config.num_frames = 5;
    config.roi_margin_mode_value = roi_margin_mode::off;

    global_data data(std::move(config));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);

    const std::vector<sf::rectangle> frame0_rectangles = {
            sf::rectangle(40, 40, 50, 50),
            sf::rectangle(40, 40, 50, 50),
    };
    const std::vector<split_rectangle> frame0_split = {
            split_rectangle(frame0_rectangles[0], 7001U, 0U, sf::yuv_color::blue, 0.9F, false),
    };
    const auto frame0_result =
            perform_detection_packing(data_view, data_cfg, frame0_rectangles, frame0_split, false, 0);

    ASSERT_EQ(frame0_result.original_rois.size(), frame0_rectangles.size());
    ASSERT_EQ(frame0_result.packed_rectangles.size(), frame0_rectangles.size());
    ASSERT_EQ(frame0_result.original_to_packed_mapping.size(), frame0_rectangles.size());
    assert_result_is_bounded_and_valid(frame0_result, data.params.width, data.params.height);

    const std::vector<sf::rectangle> frame1_rectangles = {
            sf::rectangle(42, 42, 50, 50),
            sf::rectangle(42, 42, 50, 50),
    };
    const std::vector<split_rectangle> frame1_split = {
            split_rectangle(frame1_rectangles[0], 7001U, 0U, sf::yuv_color::blue, 0.9F, false),
    };
    const auto frame1_result =
            perform_detection_packing(data_view, data_cfg, frame1_rectangles, frame1_split, false, 1);

    ASSERT_EQ(frame1_result.original_rois.size(), frame1_rectangles.size());
    ASSERT_EQ(frame1_result.packed_rectangles.size(), frame1_rectangles.size());
    ASSERT_EQ(frame1_result.original_to_packed_mapping.size(), frame1_rectangles.size());
    assert_result_is_bounded_and_valid(frame1_result, data.params.width, data.params.height);
}

TEST_F(RoiDetectionPackingTest, ReorderedSplitRectanglesRemainDeterministicAndBounded) {
    auto config = create_base_config();
    config.num_frames = 5;
    config.roi_margin_mode_value = roi_margin_mode::off;

    global_data run_a(std::move(config));
    pri_packer_view run_a_view(run_a.packer);
    const auto run_a_cfg = make_packing_config(run_a);

    const std::vector<sf::rectangle> frame0_rectangles = {
            sf::rectangle(20, 40, 30, 30),
            sf::rectangle(200, 50, 30, 30),
    };
    const std::vector<split_rectangle> frame0_split = {
            split_rectangle(frame0_rectangles[0], 111U, 0U, sf::yuv_color::blue, 0.9F, false),
            split_rectangle(frame0_rectangles[1], 222U, 0U, sf::yuv_color::blue, 0.9F, false),
    };
    const auto run_a_frame0 =
            perform_detection_packing(run_a_view, run_a_cfg, frame0_rectangles, frame0_split, false, 0);

    const std::vector<sf::rectangle> frame1_rectangles = {
            sf::rectangle(24, 40, 30, 30),
            sf::rectangle(204, 50, 30, 30),
    };
    const std::vector<split_rectangle> frame1_split_reordered = {
            split_rectangle(frame1_rectangles[1], 222U, 0U, sf::yuv_color::blue, 0.9F, false),
            split_rectangle(frame1_rectangles[0], 111U, 0U, sf::yuv_color::blue, 0.9F, false),
    };
    const auto run_a_frame1 =
            perform_detection_packing(run_a_view, run_a_cfg, frame1_rectangles, frame1_split_reordered, false, 1);

    assert_result_is_bounded_and_valid(run_a_frame0, run_a.params.width, run_a.params.height);
    assert_result_is_bounded_and_valid(run_a_frame1, run_a.params.width, run_a.params.height);

    auto config_b = create_base_config();
    config_b.num_frames = 5;
    config_b.roi_margin_mode_value = roi_margin_mode::off;
    global_data run_b(std::move(config_b));
    pri_packer_view run_b_view(run_b.packer);
    const auto run_b_cfg = make_packing_config(run_b);

    const auto run_b_frame0 =
            perform_detection_packing(run_b_view, run_b_cfg, frame0_rectangles, frame0_split, false, 0);
    const auto run_b_frame1 =
            perform_detection_packing(run_b_view, run_b_cfg, frame1_rectangles, frame1_split_reordered, false, 1);

    EXPECT_EQ(run_a_frame0.packed_rectangles, run_b_frame0.packed_rectangles);
    EXPECT_EQ(run_a_frame0.original_rois, run_b_frame0.original_rois);
    EXPECT_EQ(run_a_frame0.original_to_packed_mapping, run_b_frame0.original_to_packed_mapping);

    EXPECT_EQ(run_a_frame1.packed_rectangles, run_b_frame1.packed_rectangles);
    EXPECT_EQ(run_a_frame1.original_rois, run_b_frame1.original_rois);
    EXPECT_EQ(run_a_frame1.original_to_packed_mapping, run_b_frame1.original_to_packed_mapping);
}

TEST_F(RoiDetectionPackingTest, UnknownIdFallbackAcrossFramesKeepsOutputsValid) {
    auto config = create_base_config();
    config.num_frames = 5;
    config.roi_margin_mode_value = roi_margin_mode::off;

    global_data data(std::move(config));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);

    const std::vector<sf::rectangle> frame0_rectangles = {
            sf::rectangle(30, 120, 40, 40),
            sf::rectangle(120, 120, 40, 40),
            sf::rectangle(220, 120, 40, 40),
    };
    const std::vector<split_rectangle> frame0_split = {
            split_rectangle(frame0_rectangles[0], 510U, 0U, sf::yuv_color::blue, 0.9F, false),
            split_rectangle(frame0_rectangles[1], 620U, 0U, sf::yuv_color::blue, 0.9F, false),
    };
    const auto frame0_result =
            perform_detection_packing(data_view, data_cfg, frame0_rectangles, frame0_split, false, 0);
    assert_result_is_bounded_and_valid(frame0_result, data.params.width, data.params.height);

    const std::vector<sf::rectangle> frame1_rectangles = {
            sf::rectangle(34, 120, 40, 40),
            sf::rectangle(124, 120, 40, 40),
            sf::rectangle(224, 120, 40, 40),
    };
    const std::vector<split_rectangle> frame1_split;
    const auto frame1_result =
            perform_detection_packing(data_view, data_cfg, frame1_rectangles, frame1_split, false, 1);

    ASSERT_EQ(frame1_result.original_rois.size(), frame1_rectangles.size());
    ASSERT_EQ(frame1_result.packed_rectangles.size(), frame1_rectangles.size());
    ASSERT_EQ(frame1_result.original_to_packed_mapping.size(), frame1_rectangles.size());
    assert_result_is_bounded_and_valid(frame1_result, data.params.width, data.params.height);
}
