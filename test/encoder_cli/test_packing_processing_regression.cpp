// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../../encoder_cli/src/processing/roi_detection_processing/processing_modes/packing/packing_processing.h"
#include "../../encoder_cli/src/roi/rectangle_splitter.h"
#include "../common/gst_test_fixture.h"
#include "../common/integration_test_helpers.h"

namespace {
    using rectangle_key = std::array<int, 4>;
    using rectangle_counter = std::map<rectangle_key, int>;

    struct display_fixture_frame {
        int frame_width = 0;
        int frame_height = 0;
        rectangle_counter border_rectangles;
    };

    struct copy_rectangle_pair {
        sf::rectangle source;
        sf::rectangle destination;
    };

    [[nodiscard]] encoder_config create_base_config() {
        encoder_config config;
        config.width = 320;
        config.height = 240;
        config.framerate = 30;
        config.input_type_value = input_type::file;
        config.pack_rois = true;
        config.background_mode_value = background_mode::off;
        config.effective_background_mode_value = background_mode::off;
        config.packer_type_value = packer_type::low_delay;
        config.num_frames = 1;
        return config;
    }

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

    [[nodiscard]] std::vector<split_rectangle>
    create_split_rectangles_for(const std::vector<sf::rectangle> &rectangles) {
        std::vector<split_rectangle> split_rectangles;
        split_rectangles.reserve(rectangles.size());
        for (size_t index = 0; index < rectangles.size(); ++index) {
            split_rectangles.emplace_back(rectangles[index], static_cast<uint32_t>(1000 + index), 0,
                                          sf::yuv_color::blue, 0.9F, false);
        }
        return split_rectangles;
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

    [[nodiscard]] rectangle_key to_key(const sf::rectangle &rect) {
        return {rect.left, rect.top, rect.width, rect.height};
    }

    [[nodiscard]] sf::rectangle to_rectangle(const rectangle_key &key) {
        return sf::rectangle(key[0], key[1], key[2], key[3]);
    }

    [[nodiscard]] rectangle_counter counter_from_rectangles(const std::vector<sf::rectangle> &rectangles) {
        rectangle_counter counter;
        for (const auto &rect: rectangles) {
            counter[to_key(rect)]++;
        }
        return counter;
    }

    [[nodiscard]] rectangle_counter subtract_counters(const rectangle_counter &left, const rectangle_counter &right) {
        rectangle_counter diff;
        for (const auto &[key, left_count]: left) {
            const int right_count = right.contains(key) ? right.at(key) : 0;
            if (left_count > right_count) {
                diff[key] = left_count - right_count;
            }
        }
        return diff;
    }

    [[nodiscard]] std::string counter_to_string(const rectangle_counter &counter) {
        if (counter.empty()) {
            return "[]";
        }
        std::ostringstream out;
        out << "[";
        bool first = true;
        for (const auto &[key, count]: counter) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << "{count=" << count << ", rect=(" << key[0] << "," << key[1] << "," << key[2] << "," << key[3]
                << ")}";
        }
        out << "]";
        return out.str();
    }

    [[nodiscard]] std::optional<int> parse_int_field(const std::string &line, const std::string &field_name) {
        const std::string marker = "\"" + field_name + "\":";
        const size_t marker_pos = line.find(marker);
        if (marker_pos == std::string::npos) {
            return std::nullopt;
        }
        size_t value_start = marker_pos + marker.size();
        if (value_start >= line.size()) {
            return std::nullopt;
        }
        size_t value_end = value_start;
        if (line[value_end] == '-') {
            value_end++;
        }
        while (value_end < line.size() && std::isdigit(static_cast<unsigned char>(line[value_end])) != 0) {
            value_end++;
        }
        if (value_end == value_start || (line[value_start] == '-' && value_end == value_start + 1)) {
            return std::nullopt;
        }
        return std::stoi(line.substr(value_start, value_end - value_start));
    }

    [[nodiscard]] std::optional<uint64_t> parse_uint64_field(const std::string &line, const std::string &field_name) {
        const std::string marker = "\"" + field_name + "\":";
        const size_t marker_pos = line.find(marker);
        if (marker_pos == std::string::npos) {
            return std::nullopt;
        }

        size_t value_start = marker_pos + marker.size();
        if (value_start >= line.size()) {
            return std::nullopt;
        }
        if (line.compare(value_start, 4, "null") == 0) {
            return std::nullopt;
        }

        size_t value_end = value_start;
        while (value_end < line.size() && std::isdigit(static_cast<unsigned char>(line[value_end])) != 0) {
            value_end++;
        }
        if (value_end == value_start) {
            return std::nullopt;
        }

        return static_cast<uint64_t>(std::stoull(line.substr(value_start, value_end - value_start)));
    }

    [[nodiscard]] std::vector<sf::rectangle> parse_rectangles_from_display_line(const std::string &line) {
        static const std::regex rectangle_regex(R"(\{"x":(-?\d+),"y":(-?\d+),"w":(-?\d+),"h":(-?\d+)\})");
        std::vector<sf::rectangle> rectangles;
        for (std::sregex_iterator it(line.begin(), line.end(), rectangle_regex), end; it != end; ++it) {
            const auto &match = *it;
            rectangles.emplace_back(std::stoi(match[1].str()), std::stoi(match[2].str()), std::stoi(match[3].str()),
                                    std::stoi(match[4].str()));
        }
        return rectangles;
    }

    [[nodiscard]] std::vector<copy_rectangle_pair> parse_pairs_from_copy_line(const std::string &line) {
        static const std::regex pair_regex(
                R"(\{"source":\{"x":(-?\d+),"y":(-?\d+),"w":(-?\d+),"h":(-?\d+)\},"destination":\{"x":(-?\d+),"y":(-?\d+),"w":(-?\d+),"h":(-?\d+)\}\})");
        std::vector<copy_rectangle_pair> pairs;
        for (std::sregex_iterator it(line.begin(), line.end(), pair_regex), end; it != end; ++it) {
            const auto &match = *it;
            const sf::rectangle source(std::stoi(match[1].str()), std::stoi(match[2].str()), std::stoi(match[3].str()),
                                       std::stoi(match[4].str()));
            const sf::rectangle destination(std::stoi(match[5].str()), std::stoi(match[6].str()),
                                            std::stoi(match[7].str()), std::stoi(match[8].str()));
            pairs.push_back({source, destination});
        }
        return pairs;
    }

    [[nodiscard]] std::map<int, display_fixture_frame>
    parse_display_fixture(const std::filesystem::path &fixture_path) {
        std::ifstream input(fixture_path);
        if (!input.is_open()) {
            return {};
        }

        std::map<int, display_fixture_frame> frames;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }
            const auto frame_number = parse_int_field(line, "frame_number");
            const auto frame_width = parse_int_field(line, "frame_width");
            const auto frame_height = parse_int_field(line, "frame_height");
            if (!frame_number || !frame_width || !frame_height) {
                continue;
            }
            const auto rectangles = parse_rectangles_from_display_line(line);
            frames[*frame_number] = display_fixture_frame{
                    .frame_width = *frame_width,
                    .frame_height = *frame_height,
                    .border_rectangles = counter_from_rectangles(rectangles),
            };
        }

        return frames;
    }

    [[nodiscard]] std::map<int, std::vector<copy_rectangle_pair>>
    parse_copy_fixture(const std::filesystem::path &fixture_path) {
        std::ifstream input(fixture_path);
        if (!input.is_open()) {
            return {};
        }

        std::map<int, std::vector<copy_rectangle_pair>> frames;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line.find("\"stage\":\"copy_rectangles\"") == std::string::npos) {
                continue;
            }
            const auto frame_number = parse_int_field(line, "frame_number");
            if (!frame_number) {
                continue;
            }
            frames[*frame_number] = parse_pairs_from_copy_line(line);
        }

        return frames;
    }

    [[nodiscard]] std::optional<std::string> read_first_trace_line_for_stage(const std::filesystem::path &trace_path,
                                                                             const std::string &stage_name) {
        std::ifstream input(trace_path);
        if (!input.is_open()) {
            return std::nullopt;
        }

        const std::string stage_marker = "\"stage\":\"" + stage_name + "\"";
        std::string line;
        while (std::getline(input, line)) {
            if (line.find(stage_marker) != std::string::npos) {
                return line;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::set<std::string> parse_pair_keys_from_trace_line(const std::string &line) {
        static const std::regex pair_key_regex("\"pair_key\":\"([^\"]+)\"");
        std::set<std::string> keys;
        for (std::sregex_iterator it(line.begin(), line.end(), pair_key_regex), end; it != end; ++it) {
            keys.insert((*it)[1].str());
        }
        return keys;
    }

    [[nodiscard]] std::unordered_map<uint64_t, std::set<std::string>>
    parse_encoder_pri_trace_by_pts(const std::filesystem::path &trace_path) {
        std::unordered_map<uint64_t, std::set<std::string>> by_pts;
        std::ifstream input(trace_path);
        if (!input.is_open()) {
            return by_pts;
        }

        std::string line;
        while (std::getline(input, line)) {
            if (line.find("\"stage\":\"pri_regions\"") == std::string::npos) {
                continue;
            }
            const auto pts = parse_uint64_field(line, "pts");
            if (!pts.has_value()) {
                continue;
            }
            by_pts[*pts] = parse_pair_keys_from_trace_line(line);
        }

        return by_pts;
    }

    [[nodiscard]] std::unordered_map<uint64_t, std::set<std::string>>
    parse_rtp_pri_trace_by_pts(const std::filesystem::path &trace_path) {
        std::unordered_map<uint64_t, std::set<std::string>> by_pts;
        std::ifstream input(trace_path);
        if (!input.is_open()) {
            return by_pts;
        }

        std::string line;
        while (std::getline(input, line)) {
            if (line.find("\"key_kind\":\"pts\"") == std::string::npos) {
                continue;
            }
            const auto key_value = parse_uint64_field(line, "key_value");
            if (!key_value.has_value()) {
                continue;
            }
            by_pts[*key_value] = parse_pair_keys_from_trace_line(line);
        }

        return by_pts;
    }

    void assert_trace_pair_key_parity_for_shared_pts(const std::filesystem::path &encoder_trace_path,
                                                     const std::filesystem::path &rtp_trace_path) {
        const auto encoder_by_pts = parse_encoder_pri_trace_by_pts(encoder_trace_path);
        const auto rtp_by_pts = parse_rtp_pri_trace_by_pts(rtp_trace_path);

        ASSERT_FALSE(encoder_by_pts.empty()) << "Encoder PRI trace is empty.";
        ASSERT_FALSE(rtp_by_pts.empty()) << "RTP PRI trace is empty.";

        size_t shared_pts_count = 0U;
        for (const auto &[pts, encoder_keys]: encoder_by_pts) {
            const auto rtp_it = rtp_by_pts.find(pts);
            if (rtp_it == rtp_by_pts.end()) {
                continue;
            }
            ++shared_pts_count;
            EXPECT_EQ(encoder_keys, rtp_it->second) << "PRI pair-key mismatch for shared PTS " << pts;
        }

        EXPECT_GT(shared_pts_count, 0U) << "No shared PTS keys found between encoder and RTP PRI traces.";
    }

    [[nodiscard]] sf::rectangle compute_timestamp_rectangle(const int frame_width, const int frame_height) {
        const int rect_x = 10;
        const int rect_y = std::max(0, frame_height - 12 - 10);
        const int rect_w = std::max(0, std::min(107, frame_width - rect_x));
        const int rect_h = std::max(0, std::min(12, frame_height - rect_y));
        return sf::rectangle(rect_x, rect_y, rect_w, rect_h);
    }

    [[nodiscard]] rectangle_counter build_effective_copy_counter(const std::vector<copy_rectangle_pair> &pairs,
                                                                 const int frame_width, const int frame_height,
                                                                 const bool ignore_timestamp) {
        std::vector<sf::rectangle> effective_rectangles;
        effective_rectangles.reserve(pairs.size());
        for (const auto &pair: pairs) {
            const auto effective =
                    compute_effective_copy_destination(pair.source, pair.destination, frame_width, frame_height);
            if (effective.has_value()) {
                effective_rectangles.push_back(*effective);
            }
        }
        auto counter = counter_from_rectangles(effective_rectangles);
        if (ignore_timestamp) {
            const auto timestamp_key = to_key(compute_timestamp_rectangle(frame_width, frame_height));
            if (counter.contains(timestamp_key)) {
                counter[timestamp_key]--;
                if (counter[timestamp_key] <= 0) {
                    counter.erase(timestamp_key);
                }
            }
        }
        return counter;
    }

    void assert_effective_copy_equals_border(const std::filesystem::path &display_fixture,
                                             const std::filesystem::path &copy_fixture) {
        const auto display_frames = parse_display_fixture(display_fixture);
        const auto copy_frames = parse_copy_fixture(copy_fixture);

        ASSERT_FALSE(display_frames.empty()) << "Display fixture contains no parsable frames: " << display_fixture;
        ASSERT_FALSE(copy_frames.empty()) << "Copy fixture contains no parsable frames: " << copy_fixture;
        ASSERT_EQ(display_frames.size(), copy_frames.size()) << "Fixture frame count mismatch.";

        for (const auto &[frame_number, display_frame]: display_frames) {
            const auto copy_it = copy_frames.find(frame_number);
            ASSERT_NE(copy_it, copy_frames.end()) << "Missing copy frame " << frame_number;

            const auto effective_copy = build_effective_copy_counter(copy_it->second, display_frame.frame_width,
                                                                     display_frame.frame_height, true);
            const auto only_in_copy = subtract_counters(effective_copy, display_frame.border_rectangles);
            const auto only_in_border = subtract_counters(display_frame.border_rectangles, effective_copy);

            EXPECT_TRUE(only_in_copy.empty() && only_in_border.empty())
                    << "Frame " << frame_number << " mismatch."
                    << " only_in_copy=" << counter_to_string(only_in_copy)
                    << " only_in_border=" << counter_to_string(only_in_border);
        }
    }

    [[nodiscard]] std::vector<sf::rectangle> extract_packed_regions(const sf::packed_regions_info &pri) {
        std::vector<sf::rectangle> packed_regions;
        packed_regions.reserve(pri.regions.size());
        for (const auto &region: pri.regions) {
            packed_regions.push_back(pri_region_to_rectangle(pri, region));
        }
        return packed_regions;
    }

    void assert_unique_non_overlapping_destinations(const std::vector<sf::rectangle> &packed_regions) {
        std::set<rectangle_key> packed_geometry_keys;
        for (const auto &packed: packed_regions) {
            packed_geometry_keys.insert(to_key(packed));
        }

        EXPECT_EQ(packed_geometry_keys.size(), packed_regions.size())
                << "Merged ROIs must not produce duplicate packed destination geometries in PRI metadata.";

        for (size_t left_index = 0; left_index < packed_regions.size(); ++left_index) {
            for (size_t right_index = left_index + 1; right_index < packed_regions.size(); ++right_index) {
                EXPECT_FALSE(rectangles_overlap(packed_regions[left_index], packed_regions[right_index]))
                        << "Packed PRI regions overlap at indices " << left_index << " and " << right_index;
            }
        }
    }

    void assert_unique_pri_region_pairs(const sf::packed_regions_info &pri) {
        const unsigned int log2_unit_size = pri.log2_unit_size.value_or(0U);
        const int unit = 1 << static_cast<int>(log2_unit_size);
        std::set<std::array<int, 8>> pair_keys;

        for (const auto &region: pri.regions) {
            const sf::rectangle packed = pri_region_to_rectangle(pri, region);
            const int target_x = static_cast<int>(region.target_region_top_left_in_units_x.value_or(0U)) * unit;
            const int target_y = static_cast<int>(region.target_region_top_left_in_units_y.value_or(0U)) * unit;
            const int target_w = packed.width;
            const int target_h = packed.height;
            pair_keys.insert(
                    {packed.left, packed.top, packed.width, packed.height, target_x, target_y, target_w, target_h});
        }

        EXPECT_EQ(pair_keys.size(), pri.regions.size())
                << "Encoder PRI output contains duplicate packed->target region pairs.";
    }
} // namespace

class PackingProcessingRegressionTest : public GStreamerTestFixture {};

TEST_F(PackingProcessingRegressionTest, MergedRoisProduceUniquePackedDestinationsInMetadata) {
    global_data data(std::move(create_base_config()));

    auto input_buffer_owned = create_i420_buffer(data.params.width, data.params.height);
    ASSERT_NE(input_buffer_owned, nullptr);

    const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_owned.get(), sf::no_delete_deleter{});
    const frame_properties props{data.params.width, data.params.height, "I420"};
    auto output_buffer = create_output_buffer(input_buffer, props);
    ASSERT_NE(output_buffer, nullptr);

    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(20, 20, 80, 80),
            sf::rectangle(40, 40, 80, 80),
    };
    const auto split_rectangles = create_split_rectangles_for(input_rectangles);

    packing_process_frame(data, input_buffer, output_buffer, props, input_rectangles, split_rectangles, false, 0);

    const auto pri_opt = data.packed_regions_queue.try_pop();
    ASSERT_TRUE(pri_opt.has_value());
    ASSERT_FALSE(pri_opt->regions.cancel_flag);
    assert_unique_pri_region_pairs(pri_opt->regions);
    assert_unique_non_overlapping_destinations(extract_packed_regions(pri_opt->regions));
}

TEST_F(PackingProcessingRegressionTest, PriTraceOutputContainsNoDuplicatePairsForFrame) {
    auto config = create_base_config();
    const auto trace_root = std::filesystem::temp_directory_path() / "encoder_pri_trace_test";
    std::error_code remove_error;
    std::filesystem::remove_all(trace_root, remove_error);
    config.output_directory = trace_root.string();
    global_data data(std::move(config));

    auto input_buffer_owned = create_i420_buffer(data.params.width, data.params.height);
    ASSERT_NE(input_buffer_owned, nullptr);

    const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_owned.get(), sf::no_delete_deleter{});
    const frame_properties props{data.params.width, data.params.height, "I420"};
    auto output_buffer = create_output_buffer(input_buffer, props);
    ASSERT_NE(output_buffer, nullptr);

    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(20, 20, 90, 90),
            sf::rectangle(44, 44, 88, 88),
            sf::rectangle(224, 28, 72, 72),
            sf::rectangle(212, 176, 68, 68),
    };
    packing_process_frame(data, input_buffer, output_buffer, props, input_rectangles,
                          create_split_rectangles_for(input_rectangles), false, 0);

    const std::vector<std::filesystem::path> trace_candidates = {
            trace_root / "tracker_dump" / "roi_packing_trace.jsonl",
            std::filesystem::current_path() / "tracker_dump" / "roi_packing_trace.jsonl",
    };

    std::optional<std::string> pri_line_opt;
    for (const auto &candidate: trace_candidates) {
        if (!std::filesystem::exists(candidate)) {
            continue;
        }
        pri_line_opt = read_first_trace_line_for_stage(candidate, "pri_regions");
        if (pri_line_opt.has_value()) {
            break;
        }
    }

    ASSERT_TRUE(pri_line_opt.has_value()) << "Missing pri_regions stage in ROI packing trace candidates.";
    EXPECT_NE(pri_line_opt->find("\"duplicate_pair_count\":0"), std::string::npos)
            << "PRI trace indicates duplicate packed->target pairs: " << *pri_line_opt;
    EXPECT_NE(pri_line_opt->find("\"regions\":"), std::string::npos)
            << "PRI trace line must include serialized regions for comparison.";

    std::filesystem::remove_all(trace_root, remove_error);
}

TEST_F(PackingProcessingRegressionTest, PriTraceComparisonParsesAndMatchesSharedPtsKeys) {
    const auto temp_root = std::filesystem::temp_directory_path() / "pri_trace_compare_test";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_root, remove_error);
    std::filesystem::create_directories(temp_root);

    const auto encoder_trace_path = temp_root / "encoder_roi_packing_trace.jsonl";
    const auto rtp_trace_path = temp_root / "rtp_pri_trace.jsonl";

    {
        std::ofstream encoder_trace(encoder_trace_path);
        ASSERT_TRUE(encoder_trace.is_open());
        encoder_trace << "{\"frame_number\":10,\"stage\":\"pri_regions\",\"pts\":9000,\"dts\":null,"
                         "\"regions\":[{\"pair_key\":\"1:2:32:32|100:120:32:32\"},"
                         "{\"pair_key\":\"44:2:32:32|300:120:32:32\"}],\"duplicate_pair_count\":0}\n";
    }

    {
        std::ofstream rtp_trace(rtp_trace_path);
        ASSERT_TRUE(rtp_trace.is_open());
        rtp_trace << "{\"matched_frame_index\":1,\"stream_id\":\"stream0\",\"key_kind\":\"pts\",\"key_value\":9000,"
                     "\"cancel_flag\":false,\"regions\":[{\"pair_key\":\"1:2:32:32|100:120:32:32\"},"
                     "{\"pair_key\":\"44:2:32:32|300:120:32:32\"}],\"duplicate_pair_count\":0}\n";
    }

    assert_trace_pair_key_parity_for_shared_pts(encoder_trace_path, rtp_trace_path);
    std::filesystem::remove_all(temp_root, remove_error);
}

TEST_F(PackingProcessingRegressionTest, MergedGroupAndNeighborStayUniqueAcrossFrames) {
    auto config = create_base_config();
    config.num_frames = 5;
    global_data data(std::move(config));

    const frame_properties props{data.params.width, data.params.height, "I420"};

    auto frame0_buffer_owned = create_i420_buffer(data.params.width, data.params.height);
    ASSERT_NE(frame0_buffer_owned, nullptr);
    const sf::unique_gst_buffer_no_delete_ptr frame0_input(frame0_buffer_owned.get(), sf::no_delete_deleter{});
    auto frame0_output = create_output_buffer(frame0_input, props);
    ASSERT_NE(frame0_output, nullptr);
    const std::vector<sf::rectangle> frame0_input_rectangles = {
            sf::rectangle(20, 20, 80, 80),
            sf::rectangle(44, 44, 78, 78),
            sf::rectangle(210, 30, 70, 70),
    };
    packing_process_frame(data, frame0_input, frame0_output, props, frame0_input_rectangles,
                          create_split_rectangles_for(frame0_input_rectangles), false, 0);
    const auto frame0_pri_opt = data.packed_regions_queue.try_pop();
    ASSERT_TRUE(frame0_pri_opt.has_value());
    ASSERT_FALSE(frame0_pri_opt->regions.cancel_flag);
    assert_unique_pri_region_pairs(frame0_pri_opt->regions);
    const auto frame0_regions = extract_packed_regions(frame0_pri_opt->regions);

    ASSERT_FALSE(frame0_regions.empty());
    EXPECT_LE(frame0_regions.size(), 2U)
            << "Merged-group canonicalization may collapse frame 0 to one destination if CTU overlap remains.";
    assert_unique_non_overlapping_destinations(frame0_regions);

    auto frame1_buffer_owned = create_i420_buffer(data.params.width, data.params.height);
    ASSERT_NE(frame1_buffer_owned, nullptr);
    const sf::unique_gst_buffer_no_delete_ptr frame1_input(frame1_buffer_owned.get(), sf::no_delete_deleter{});
    auto frame1_output = create_output_buffer(frame1_input, props);
    ASSERT_NE(frame1_output, nullptr);
    const std::vector<sf::rectangle> frame1_input_rectangles = {
            sf::rectangle(16, 16, 108, 108),
            sf::rectangle(48, 48, 92, 92),
            sf::rectangle(210, 30, 70, 70),
    };
    packing_process_frame(data, frame1_input, frame1_output, props, frame1_input_rectangles,
                          create_split_rectangles_for(frame1_input_rectangles), false, 1);
    const auto frame1_pri_opt = data.packed_regions_queue.try_pop();
    ASSERT_TRUE(frame1_pri_opt.has_value());
    ASSERT_FALSE(frame1_pri_opt->regions.cancel_flag);
    assert_unique_pri_region_pairs(frame1_pri_opt->regions);
    const auto frame1_regions = extract_packed_regions(frame1_pri_opt->regions);

    ASSERT_FALSE(frame1_regions.empty());
    EXPECT_LE(frame1_regions.size(), 2U)
            << "Merged-group canonicalization may collapse frame 1 to one destination if CTU overlap remains.";
    assert_unique_non_overlapping_destinations(frame1_regions);

    const auto area = [](const sf::rectangle &rect) { return rect.width * rect.height; };
    const auto max_area_in = [&area](const std::vector<sf::rectangle> &rectangles) {
        int largest_area = 0;
        for (const auto &rect: rectangles) {
            largest_area = std::max(largest_area, area(rect));
        }
        return largest_area;
    };
    const int largest_area_frame0 = max_area_in(frame0_regions);
    const int largest_area_frame1 = max_area_in(frame1_regions);
    EXPECT_GE(largest_area_frame1, largest_area_frame0)
            << "Merged source union growth should not shrink the largest packed destination unexpectedly.";
}

TEST_F(PackingProcessingRegressionTest, MappedIndexDedupDoesNotDropTrailingPackedGroups) {
    global_data data(std::move(create_base_config()));

    auto input_buffer_owned = create_i420_buffer(data.params.width, data.params.height);
    ASSERT_NE(input_buffer_owned, nullptr);
    const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_owned.get(), sf::no_delete_deleter{});
    const frame_properties props{data.params.width, data.params.height, "I420"};
    auto output_buffer = create_output_buffer(input_buffer, props);
    ASSERT_NE(output_buffer, nullptr);

    const std::vector<sf::rectangle> input_rectangles = {
            sf::rectangle(20, 20, 90, 90),
            sf::rectangle(44, 44, 88, 88),
            sf::rectangle(224, 28, 72, 72),
            sf::rectangle(212, 176, 68, 68),
    };
    packing_process_frame(data, input_buffer, output_buffer, props, input_rectangles,
                          create_split_rectangles_for(input_rectangles), false, 0);

    const auto pri_opt = data.packed_regions_queue.try_pop();
    ASSERT_TRUE(pri_opt.has_value());
    ASSERT_FALSE(pri_opt->regions.cancel_flag);
    assert_unique_pri_region_pairs(pri_opt->regions);
    const auto packed_regions = extract_packed_regions(pri_opt->regions);

    ASSERT_EQ(packed_regions.size(), 3U) << "Merged-map dedup must preserve all unique mapped packed groups.";
    assert_unique_non_overlapping_destinations(packed_regions);
}

TEST_F(PackingProcessingRegressionTest, MergedGroupDestinationsStayStableAcrossConsecutiveFrames) {
    auto config = create_base_config();
    config.num_frames = 6;
    global_data data(std::move(config));

    const frame_properties props{data.params.width, data.params.height, "I420"};
    const std::vector<std::vector<sf::rectangle>> frame_inputs = {
            {
                    sf::rectangle(20, 20, 90, 90),
                    sf::rectangle(44, 44, 88, 88),
                    sf::rectangle(224, 28, 72, 72),
                    sf::rectangle(212, 176, 68, 68),
            },
            {
                    sf::rectangle(18, 18, 96, 96),
                    sf::rectangle(46, 46, 90, 90),
                    sf::rectangle(226, 28, 72, 72),
                    sf::rectangle(212, 176, 68, 68),
            },
            {
                    sf::rectangle(16, 16, 104, 104),
                    sf::rectangle(50, 50, 88, 88),
                    sf::rectangle(226, 30, 72, 72),
                    sf::rectangle(210, 174, 72, 72),
            },
    };

    for (size_t frame_index = 0; frame_index < frame_inputs.size(); ++frame_index) {
        auto input_buffer_owned = create_i420_buffer(data.params.width, data.params.height);
        ASSERT_NE(input_buffer_owned, nullptr);
        const sf::unique_gst_buffer_no_delete_ptr input_buffer(input_buffer_owned.get(), sf::no_delete_deleter{});
        auto output_buffer = create_output_buffer(input_buffer, props);
        ASSERT_NE(output_buffer, nullptr);

        const auto &frame_rectangles = frame_inputs[frame_index];
        packing_process_frame(data, input_buffer, output_buffer, props, frame_rectangles,
                              create_split_rectangles_for(frame_rectangles), false, static_cast<int>(frame_index));

        const auto pri_opt = data.packed_regions_queue.try_pop();
        ASSERT_TRUE(pri_opt.has_value()) << "Missing PRI metadata for frame " << frame_index;
        ASSERT_FALSE(pri_opt->regions.cancel_flag) << "Unexpected sentinel metadata for frame " << frame_index;
        assert_unique_pri_region_pairs(pri_opt->regions);

        const auto packed_regions = extract_packed_regions(pri_opt->regions);
        ASSERT_EQ(packed_regions.size(), 3U)
                << "Consecutive merged frames must keep all canonical packed destinations.";
        assert_unique_non_overlapping_destinations(packed_regions);
    }
}

TEST_F(PackingProcessingRegressionTest, StrictCopyEqualsBorderOnControlFrames) {
    const auto fixture_dir = get_test_data_dir() / "tracker_dump";
    const auto display_fixture = fixture_dir / "control_display_border_rectangles.jsonl";
    const auto copy_fixture = fixture_dir / "control_copy_rectangles.jsonl";

    ASSERT_TRUE(std::filesystem::exists(display_fixture));
    ASSERT_TRUE(std::filesystem::exists(copy_fixture));

    assert_effective_copy_equals_border(display_fixture, copy_fixture);
}

TEST_F(PackingProcessingRegressionTest, StrictCopyEqualsBorderOnCapturedMismatchFrames) {
    const auto fixture_dir = get_test_data_dir() / "tracker_dump";
    const auto display_fixture = fixture_dir / "mismatch_display_border_rectangles.jsonl";
    const auto copy_fixture = fixture_dir / "mismatch_copy_rectangles.jsonl";

    ASSERT_TRUE(std::filesystem::exists(display_fixture));
    ASSERT_TRUE(std::filesystem::exists(copy_fixture));

    // Strict invariant: effective copied destination must exactly match displayed border rectangles.
    assert_effective_copy_equals_border(display_fixture, copy_fixture);
}
