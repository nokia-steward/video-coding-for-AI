// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <tuple>
#include <utility>
#include <vector>

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/roi_detection_packing.h"
#include "../../encoder_cli/src/roi/pri_packer_view.h"
#include "../../encoder_cli/src/roi/rectangle_splitter.h"
#include "../common/gst_test_fixture.h"

namespace {
    struct frame_input {
        std::vector<sf::rectangle> rectangles;
        std::vector<split_rectangle> split_rectangles;
    };

    [[nodiscard]] encoder_config create_temporal_config() {
        encoder_config config;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.input_type_value = input_type::file;
        config.pack_rois = true;
        config.background_mode_value = background_mode::off;
        config.effective_background_mode_value = background_mode::off;
        config.packer_type_value = packer_type::low_delay;
        config.num_frames = 5;
        config.roi_margin_mode_value = roi_margin_mode::off;
        return config;
    }

    [[nodiscard]] split_rectangle make_split(const sf::rectangle &rect, const uint32_t object_id,
                                             const sf::yuv_color color) {
        return split_rectangle(rect, object_id, 0U, color, 0.92F, false);
    }

    [[nodiscard]] std::vector<frame_input> create_tracker_like_sequence() {
        // Crafted to cover jitter, disappear/reappear, crossing trajectories, and mixed prediction flags.
        return {
                {
                        {sf::rectangle(40, 40, 40, 40), sf::rectangle(280, 40, 42, 40),
                         sf::rectangle(160, 180, 52, 50)},
                        {make_split(sf::rectangle(40, 40, 40, 40), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(280, 40, 42, 40), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(160, 180, 52, 50), 303U, sf::yuv_color::red)},
                },
                {
                        {sf::rectangle(44, 42, 40, 40), sf::rectangle(276, 44, 42, 40),
                         sf::rectangle(164, 176, 52, 50)},
                        {make_split(sf::rectangle(44, 42, 40, 40), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(276, 44, 42, 40), 202U, sf::yuv_color::red),
                         make_split(sf::rectangle(164, 176, 52, 50), 303U, sf::yuv_color::red)},
                },
                {
                        {sf::rectangle(48, 44, 40, 40), sf::rectangle(168, 172, 52, 50)},
                        {make_split(sf::rectangle(48, 44, 40, 40), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(168, 172, 52, 50), 303U, sf::yuv_color::blue)},
                },
                {
                        {sf::rectangle(52, 46, 40, 40), sf::rectangle(270, 48, 42, 40),
                         sf::rectangle(172, 168, 52, 50)},
                        {make_split(sf::rectangle(52, 46, 40, 40), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(270, 48, 42, 40), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(172, 168, 52, 50), 303U, sf::yuv_color::red)},
                },
                {
                        {sf::rectangle(88, 60, 44, 42), sf::rectangle(236, 56, 44, 42),
                         sf::rectangle(176, 166, 50, 50)},
                        {make_split(sf::rectangle(88, 60, 44, 42), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(236, 56, 44, 42), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(176, 166, 50, 50), 303U, sf::yuv_color::blue)},
                },
                {
                        {sf::rectangle(122, 72, 48, 44), sf::rectangle(202, 68, 48, 44),
                         sf::rectangle(180, 164, 50, 50)},
                        {make_split(sf::rectangle(122, 72, 48, 44), 101U, sf::yuv_color::red),
                         make_split(sf::rectangle(202, 68, 48, 44), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(180, 164, 50, 50), 303U, sf::yuv_color::blue)},
                },
                {
                        {sf::rectangle(140, 80, 50, 46), sf::rectangle(184, 78, 50, 46),
                         sf::rectangle(182, 162, 50, 50)},
                        {make_split(sf::rectangle(140, 80, 50, 46), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(184, 78, 50, 46), 202U, sf::yuv_color::red),
                         make_split(sf::rectangle(182, 162, 50, 50), 303U, sf::yuv_color::blue)},
                },
                {
                        {sf::rectangle(158, 86, 50, 46), sf::rectangle(166, 84, 50, 46),
                         sf::rectangle(186, 160, 50, 50)},
                        {make_split(sf::rectangle(158, 86, 50, 46), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(166, 84, 50, 46), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(186, 160, 50, 50), 303U, sf::yuv_color::red)},
                },
                {
                        {sf::rectangle(176, 94, 50, 46), sf::rectangle(148, 90, 50, 46),
                         sf::rectangle(188, 158, 50, 50)},
                        {make_split(sf::rectangle(176, 94, 50, 46), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(148, 90, 50, 46), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(188, 158, 50, 50), 303U, sf::yuv_color::blue)},
                },
                {
                        {sf::rectangle(194, 102, 50, 46), sf::rectangle(130, 98, 50, 46),
                         sf::rectangle(190, 156, 50, 50)},
                        {make_split(sf::rectangle(194, 102, 50, 46), 101U, sf::yuv_color::red),
                         make_split(sf::rectangle(130, 98, 50, 46), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(190, 156, 50, 50), 303U, sf::yuv_color::blue)},
                },
                {
                        {sf::rectangle(212, 110, 50, 46), sf::rectangle(112, 106, 50, 46)},
                        {make_split(sf::rectangle(212, 110, 50, 46), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(112, 106, 50, 46), 202U, sf::yuv_color::red)},
                },
                {
                        {sf::rectangle(230, 118, 50, 46), sf::rectangle(94, 114, 50, 46),
                         sf::rectangle(194, 154, 50, 50)},
                        {make_split(sf::rectangle(230, 118, 50, 46), 101U, sf::yuv_color::blue),
                         make_split(sf::rectangle(94, 114, 50, 46), 202U, sf::yuv_color::blue),
                         make_split(sf::rectangle(194, 154, 50, 50), 303U, sf::yuv_color::red)},
                },
        };
    }

    void assert_result_invariants(const packing_result &result, const int width, const int height) {
        ASSERT_EQ(result.original_rois.size(), result.original_to_packed_mapping.size());
        for (const auto packed_index: result.original_to_packed_mapping) {
            EXPECT_LT(packed_index, result.packed_rectangles.size());
        }

        for (const auto &rect: result.packed_rectangles) {
            ASSERT_TRUE(rect.is_valid());
            EXPECT_GE(rect.left, 0);
            EXPECT_GE(rect.top, 0);
            EXPECT_LE(rect.left + rect.width, width);
            EXPECT_LE(rect.top + rect.height, height);
        }
    }

    using packing_signature = std::tuple<std::vector<sf::rectangle>, std::vector<sf::rectangle>, std::vector<size_t>>;
} // namespace

class LowDelayInferenceTemporalRegressionTest : public GStreamerTestFixture {};

TEST_F(LowDelayInferenceTemporalRegressionTest, TrackerLikeSequenceMaintainsBoundsAndMappingValidity) {
    global_data data(std::move(create_temporal_config()));
    pri_packer_view data_view(data.packer);
    const auto data_cfg = make_packing_config(data);
    const auto sequence = create_tracker_like_sequence();

    for (size_t frame_index = 0; frame_index < sequence.size(); ++frame_index) {
        const auto &frame = sequence[frame_index];
        const auto result = perform_detection_packing(data_view, data_cfg, frame.rectangles, frame.split_rectangles,
                                                      false, static_cast<int>(frame_index));
        ASSERT_FALSE(result.packed_rectangles.empty()) << "Unexpected empty packed output at frame " << frame_index;
        assert_result_invariants(result, data.params.width, data.params.height);
    }
}

TEST_F(LowDelayInferenceTemporalRegressionTest, TrackerLikeSequenceIsDeterministicAcrossReplays) {
    const auto sequence = create_tracker_like_sequence();

    global_data run_a(std::move(create_temporal_config()));
    pri_packer_view run_a_view(run_a.packer);
    const auto run_a_cfg = make_packing_config(run_a);
    std::vector<packing_signature> signatures_a;
    signatures_a.reserve(sequence.size());
    for (size_t frame_index = 0; frame_index < sequence.size(); ++frame_index) {
        const auto &frame = sequence[frame_index];
        const auto result = perform_detection_packing(run_a_view, run_a_cfg, frame.rectangles, frame.split_rectangles,
                                                      false, static_cast<int>(frame_index));
        assert_result_invariants(result, run_a.params.width, run_a.params.height);
        signatures_a.emplace_back(result.packed_rectangles, result.original_rois, result.original_to_packed_mapping);
    }

    global_data run_b(std::move(create_temporal_config()));
    pri_packer_view run_b_view(run_b.packer);
    const auto run_b_cfg = make_packing_config(run_b);
    std::vector<packing_signature> signatures_b;
    signatures_b.reserve(sequence.size());
    for (size_t frame_index = 0; frame_index < sequence.size(); ++frame_index) {
        const auto &frame = sequence[frame_index];
        const auto result = perform_detection_packing(run_b_view, run_b_cfg, frame.rectangles, frame.split_rectangles,
                                                      false, static_cast<int>(frame_index));
        assert_result_invariants(result, run_b.params.width, run_b.params.height);
        signatures_b.emplace_back(result.packed_rectangles, result.original_rois, result.original_to_packed_mapping);
    }

    ASSERT_EQ(signatures_a.size(), signatures_b.size());
    EXPECT_EQ(signatures_a, signatures_b);
}
