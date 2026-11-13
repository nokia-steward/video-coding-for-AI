// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/processing_modes/packing/packing_processing.h"
#include "../../encoder_cli/src/roi/ctu_alignment.h"
#include "../common/gst_test_fixture.h"
#include "../common/integration_test_helpers.h"

namespace {
    [[nodiscard]] sf::unique_gst_buffer_ptr create_i420_buffer(const int width, const int height) {
        const size_t frame_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3U / 2U;
        sf::unique_gst_buffer_ptr buffer(gst_buffer_new_allocate(nullptr, frame_size, nullptr));
        if (!buffer) {
            return nullptr;
        }

        GstMapInfo map_info;
        if (gst_buffer_map(buffer.get(), &map_info, GST_MAP_WRITE)) {
            std::memset(map_info.data, 128, map_info.size);
            gst_buffer_unmap(buffer.get(), &map_info);
        }

        GST_BUFFER_PTS(buffer.get()) = 0;
        GST_BUFFER_DTS(buffer.get()) = 0;
        GST_BUFFER_DURATION(buffer.get()) = GST_SECOND / 30;
        static uint64_t next_sequence_id = 1;
        if (!attach_frame_contract_metadata_to_buffer(buffer.get(), frame_path_type::inference, next_sequence_id++)) {
            return nullptr;
        }
        return buffer;
    }

    [[nodiscard]] sf::rectangle pri_region_to_rectangle(const sf::packed_regions_info &pri, const sf::region &region) {
        const unsigned int log2_unit_size = pri.log2_unit_size.value_or(0U);
        const int unit = 1 << static_cast<int>(log2_unit_size);
        const int rect_x = static_cast<int>(region.top_left_in_units_x.value_or(0U)) * unit;
        const int rect_y = static_cast<int>(region.top_left_in_units_y.value_or(0U)) * unit;
        const int rect_w = static_cast<int>(region.width_in_units_minus1.value_or(0U) + 1U) * unit;
        const int rect_h = static_cast<int>(region.height_in_units_minus1.value_or(0U) + 1U) * unit;
        return sf::rectangle(rect_x, rect_y, rect_w, rect_h);
    }

    [[nodiscard]] bool rectangles_overlap(const sf::rectangle &left, const sf::rectangle &right) {
        return !(left.left + left.width <= right.left || right.left + right.width <= left.left ||
                 left.top + left.height <= right.top || right.top + right.height <= left.top);
    }

    void assert_strict_non_overlap(const std::vector<sf::rectangle> &packed_regions, const int frame_number) {
        std::set<std::pair<int, int>> packed_top_left_points;
        for (const auto &packed: packed_regions) {
            packed_top_left_points.emplace(packed.left, packed.top);
        }
        EXPECT_EQ(packed_top_left_points.size(), packed_regions.size())
                << "Duplicate packed destinations found in frame " << frame_number;

        for (size_t left_index = 0; left_index < packed_regions.size(); ++left_index) {
            const auto left_ctu = compute_ctu_rectangle(packed_regions[left_index], default_ctu_size);
            for (size_t right_index = left_index + 1; right_index < packed_regions.size(); ++right_index) {
                const auto right_ctu = compute_ctu_rectangle(packed_regions[right_index], default_ctu_size);
                EXPECT_FALSE(rectangles_overlap(packed_regions[left_index], packed_regions[right_index]))
                        << "Packed PRI overlap in frame " << frame_number << " between regions " << left_index
                        << " and " << right_index;
                EXPECT_FALSE(ctu_rectangles_overlap(left_ctu, right_ctu))
                        << "Packed CTU overlap in frame " << frame_number << " between regions " << left_index
                        << " and " << right_index;
            }
        }
    }
} // namespace

class LowDelayOverlapRegressionIntegrationTest : public GStreamerTestFixture {
protected:
    [[nodiscard]] std::filesystem::path
    create_temp_roi_trace_file(const std::vector<std::vector<sf::rectangle>> &frames, const std::string &filename) {
        const auto roi_trace_path = std::filesystem::temp_directory_path() / filename;
        std::ofstream roi_file(roi_trace_path);
        EXPECT_TRUE(roi_file.is_open()) << "Failed to open temp ROI trace path " << roi_trace_path.string();

        roi_file << "[";
        for (size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
            if (frame_index > 0U) {
                roi_file << ",";
            }
            roi_file << "[";
            const auto &frame_rois = frames[frame_index];
            for (size_t roi_index = 0; roi_index < frame_rois.size(); ++roi_index) {
                if (roi_index > 0U) {
                    roi_file << ",";
                }
                const auto &roi = frame_rois[roi_index];
                roi_file << "[" << roi.left << "," << roi.top << "," << (roi.left + roi.width) << ","
                         << (roi.top + roi.height) << "]";
            }
            roi_file << "]";
        }
        roi_file << "]";
        roi_file.close();

        created_roi_paths.push_back(roi_trace_path);
        return roi_trace_path;
    }

    [[nodiscard]] std::filesystem::path create_temp_config_for_roi_file(const std::filesystem::path &roi_file_path,
                                                                        const std::string &config_filename) {
        const auto temp_config_path = std::filesystem::temp_directory_path() / config_filename;
        std::ofstream config_file(temp_config_path);
        EXPECT_TRUE(config_file.is_open()) << "Failed to open temp config path " << temp_config_path.string();
        config_file << "[Common]\n";
        config_file << "input_type=udp\n";
        config_file << "width=640\n";
        config_file << "height=480\n";
        config_file << "framerate=30\n";
        config_file << "\n[UDP]\n";
        config_file << "bind_address=0.0.0.0\n";
        config_file << "port=12300\n";
        config_file << "encoding=H265\n";
        config_file << "payload_type=96\n";
        config_file << "clock_rate=90000\n";
        config_file << "latency_ms=200\n";
        config_file << "\n[ROI]\n";
        config_file << "data_file=" << roi_file_path.string() << "\n";
        config_file << "pack_rois=true\n";
        config_file << "packer_type=low_delay\n";
        config_file << "num_frames=5\n";
        config_file << "roi_fixed_margin_px=20\n";
        config_file << "roi_margin_mode=content\n";
        config_file << "inference_engine=false\n";
        config_file << "\n[Debug]\n";
        config_file << "draw_borders=false\n";
        config_file << "draw_rtp_timestamp=false\n";
        config_file << "save_frames=false\n";
        config_file.close();
        created_config_paths.push_back(temp_config_path);
        return temp_config_path;
    }

    void run_regression_for_config(const std::filesystem::path &run_config_path, const bool require_large_multi_ctu) {
        const std::string config_arg = "--config";
        const std::string config_value = run_config_path.string();
        const char *argv[] = {"test_encoder", config_arg.c_str(), config_value.c_str(), nullptr};

        command_line_parameters params = parse_command_line(3, const_cast<char **>(argv));
        encoder_config config = load_and_configure_encoder(params);

        ASSERT_TRUE(config.pack_rois);
        ASSERT_EQ(config.packer_type_value, packer_type::low_delay);
        ASSERT_EQ(config.num_frames, 5);
        ASSERT_EQ(config.roi_fixed_margin_px, 20);
        ASSERT_EQ(config.roi_margin_mode_value, roi_margin_mode::content);
        ASSERT_TRUE(config.roi_info.has_value());
        ASSERT_FALSE(config.roi_info->rois.empty());

        global_data data(std::move(config));
        const frame_properties props{data.params.width, data.params.height, "I420"};

        bool observed_merge = false;
        bool observed_large_multi_ctu = false;
        for (const auto &[frame_number, frame_rois]: data.params.roi_info->rois) {
            auto input_buffer_owned = create_i420_buffer(data.params.width, data.params.height);
            ASSERT_NE(input_buffer_owned, nullptr);
            const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_owned.get(), sf::no_delete_deleter{});

            auto output_buffer = create_output_buffer(input_buffer, props);
            ASSERT_NE(output_buffer, nullptr);

            for (const auto &roi: frame_rois) {
                if (roi.width > default_ctu_size || roi.height > default_ctu_size) {
                    observed_large_multi_ctu = true;
                    break;
                }
            }

            std::vector<split_rectangle> split_rectangles;
            split_rectangles.reserve(frame_rois.size());
            for (size_t index = 0; index < frame_rois.size(); ++index) {
                split_rectangles.emplace_back(frame_rois[index], static_cast<uint32_t>(5000 + index), 0,
                                              sf::yuv_color::blue, 0.95F, false);
            }

            packing_process_frame(data, input_buffer, output_buffer, props, frame_rois, split_rectangles, false,
                                  frame_number);

            const auto pri_opt = data.packed_regions_queue.try_pop();
            ASSERT_TRUE(pri_opt.has_value()) << "Missing PRI metadata for frame " << frame_number;
            ASSERT_FALSE(pri_opt->regions.cancel_flag) << "Unexpected sentinel metadata for frame " << frame_number;

            if (pri_opt->regions.regions.size() < frame_rois.size()) {
                observed_merge = true;
            }

            std::vector<sf::rectangle> packed_regions;
            packed_regions.reserve(pri_opt->regions.regions.size());
            for (const auto &region: pri_opt->regions.regions) {
                packed_regions.push_back(pri_region_to_rectangle(pri_opt->regions, region));
            }
            assert_strict_non_overlap(packed_regions, frame_number);
        }

        EXPECT_TRUE(observed_merge) << "Fixture did not exercise merged-ROI behavior.";
        if (require_large_multi_ctu) {
            EXPECT_TRUE(observed_large_multi_ctu) << "Fixture did not include large multi-CTU ROI inputs.";
        }
    }

    void SetUp() override {
        GStreamerTestFixture::SetUp();
        ensure_test_directories();

        roi_data_path = get_test_data_dir() / "roi_data" / "BasketballDrill_640x480_30.txt";
        overlap_window_a_path = get_test_data_dir() / "roi_data" / "OverlapWindowA_640x480_30.txt";
        overlap_window_b_path = get_test_data_dir() / "roi_data" / "OverlapWindowB_640x480_30.txt";

        ASSERT_TRUE(std::filesystem::exists(roi_data_path))
                << "ROI trace fixture missing at " << roi_data_path.string();
        ASSERT_TRUE(std::filesystem::exists(overlap_window_a_path))
                << "ROI trace fixture missing at " << overlap_window_a_path.string();
        ASSERT_TRUE(std::filesystem::exists(overlap_window_b_path))
                << "ROI trace fixture missing at " << overlap_window_b_path.string();

        config_path = create_temp_config_for_roi_file(roi_data_path, "test_low_delay_overlap_regression.ini");
        ASSERT_TRUE(std::filesystem::exists(config_path));
    }

    void TearDown() override {
        for (const auto &config_to_remove: created_config_paths) {
            std::error_code ec;
            std::filesystem::remove(config_to_remove, ec);
        }
        for (const auto &roi_to_remove: created_roi_paths) {
            std::error_code ec;
            std::filesystem::remove(roi_to_remove, ec);
        }
        GStreamerTestFixture::TearDown();
    }

    std::filesystem::path roi_data_path;
    std::filesystem::path overlap_window_a_path;
    std::filesystem::path overlap_window_b_path;
    std::filesystem::path config_path;
    std::vector<std::filesystem::path> created_config_paths;
    std::vector<std::filesystem::path> created_roi_paths;
};

TEST_F(LowDelayOverlapRegressionIntegrationTest, ConfigDrivenOverlapTraceProducesValidPackedMetadata) {
    run_regression_for_config(config_path, false);
}

TEST_F(LowDelayOverlapRegressionIntegrationTest, WindowedLargeRoiTracesMaintainStrictPixelAndCtuSeparation) {
    const auto config_window_a = create_temp_config_for_roi_file(overlap_window_a_path, "test_overlap_window_a.ini");
    const auto config_window_b = create_temp_config_for_roi_file(overlap_window_b_path, "test_overlap_window_b.ini");

    run_regression_for_config(config_window_a, true);
    run_regression_for_config(config_window_b, true);
}

TEST_F(LowDelayOverlapRegressionIntegrationTest, FailureSignatureGeometryAvoidsSentinelAndDuplicateDestinations) {
    const std::vector<std::vector<sf::rectangle>> synthetic_frames = {
            {
                    sf::rectangle(280, 168, 156, 128),
                    sf::rectangle(332, 188, 128, 120),
                    sf::rectangle(120, 110, 84, 96),
                    sf::rectangle(126, 226, 82, 94),
                    sf::rectangle(452, 76, 96, 104),
                    sf::rectangle(470, 224, 92, 102),
            },
            {
                    sf::rectangle(278, 166, 164, 132),
                    sf::rectangle(336, 192, 132, 116),
                    sf::rectangle(118, 108, 88, 98),
                    sf::rectangle(128, 226, 84, 94),
                    sf::rectangle(452, 80, 98, 104),
                    sf::rectangle(468, 222, 96, 104),
            },
            {
                    sf::rectangle(276, 164, 170, 136),
                    sf::rectangle(340, 196, 136, 120),
                    sf::rectangle(118, 108, 90, 100),
                    sf::rectangle(130, 226, 84, 96),
                    sf::rectangle(450, 80, 100, 108),
                    sf::rectangle(466, 220, 98, 104),
            },
            {
                    sf::rectangle(274, 162, 174, 140),
                    sf::rectangle(344, 198, 140, 124),
                    sf::rectangle(118, 108, 92, 102),
                    sf::rectangle(132, 226, 86, 98),
                    sf::rectangle(448, 82, 104, 110),
                    sf::rectangle(464, 220, 100, 106),
            },
            {
                    sf::rectangle(272, 160, 178, 144),
                    sf::rectangle(348, 200, 144, 126),
                    sf::rectangle(118, 108, 92, 102),
                    sf::rectangle(134, 226, 88, 98),
                    sf::rectangle(446, 84, 106, 112),
                    sf::rectangle(462, 220, 102, 106),
            },
    };

    const auto roi_trace_path =
            create_temp_roi_trace_file(synthetic_frames, "test_low_delay_failure_signature_trace_640x480_30.txt");
    const auto config_path =
            create_temp_config_for_roi_file(roi_trace_path, "test_low_delay_failure_signature_trace.ini");

    run_regression_for_config(config_path, true);
}
