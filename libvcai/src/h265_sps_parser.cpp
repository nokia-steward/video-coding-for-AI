// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "h265_sps_parser.h"

#include "bitstream.h"

#include <algorithm>
#include <vector>

namespace {
    struct start_code_info {
        size_t offset = 0;
        size_t size = 0;
    };

    [[nodiscard]] std::optional<start_code_info> find_start_code(const uint8_t *data, const size_t length) {
        if (length >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
            return start_code_info{0, 4};
        }
        if (length >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
            return start_code_info{0, 3};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<uint8_t> remove_emulation_prevention_bytes(const uint8_t *data, const size_t length) {
        auto rbsp = std::vector<uint8_t>();
        rbsp.reserve(length);

        int zero_count = 0;
        for (size_t index = 0; index < length; ++index) {
            const auto byte = data[index];
            if (zero_count == 2 && byte == 0x03) {
                zero_count = 0;
                continue;
            }
            rbsp.push_back(byte);
            zero_count = byte == 0x00 ? zero_count + 1 : 0;
        }
        return rbsp;
    }

    [[nodiscard]] int32_t read_se(sf::bitstream_reader &reader) {
        const auto code_num = reader.read_ue();
        const auto signed_value = static_cast<int32_t>((code_num + 1U) / 2U);
        return (code_num % 2U == 0U) ? -signed_value : signed_value;
    }

    void skip_bits(sf::bitstream_reader &reader, const int count) {
        const auto value = reader.read_bits(count);
        static_cast<void>(value);
    }

    void skip_ue(sf::bitstream_reader &reader) {
        const auto value = reader.read_ue();
        static_cast<void>(value);
    }

    void skip_bit(sf::bitstream_reader &reader) {
        const auto value = reader.read_bit();
        static_cast<void>(value);
    }

    void skip_profile_tier_level(sf::bitstream_reader &reader, const uint8_t max_sub_layers_minus1) {
        skip_bits(reader, 2); // general_profile_space
        skip_bits(reader, 1); // general_tier_flag
        skip_bits(reader, 5); // general_profile_idc
        skip_bits(reader, 32); // general_profile_compatibility_flags
        skip_bits(reader, 1); // general_progressive_source_flag
        skip_bits(reader, 1); // general_interlaced_source_flag
        skip_bits(reader, 1); // general_non_packed_constraint_flag
        skip_bits(reader, 1); // general_frame_only_constraint_flag
        skip_bits(reader, 44); // general_reserved_zero_44bits
        skip_bits(reader, 8); // general_level_idc

        std::vector<uint8_t> sub_layer_profile_present(max_sub_layers_minus1);
        std::vector<uint8_t> sub_layer_level_present(max_sub_layers_minus1);
        for (uint8_t index = 0; index < max_sub_layers_minus1; ++index) {
            sub_layer_profile_present[index] = reader.read_bit();
            sub_layer_level_present[index] = reader.read_bit();
        }

        if (max_sub_layers_minus1 > 0) {
            for (uint8_t index = max_sub_layers_minus1; index < 8; ++index) {
                skip_bits(reader, 2); // reserved_zero_2bits
            }
        }

        for (uint8_t index = 0; index < max_sub_layers_minus1; ++index) {
            if (sub_layer_profile_present[index] != 0) {
                skip_bits(reader, 2); // sub_layer_profile_space
                skip_bits(reader, 1); // sub_layer_tier_flag
                skip_bits(reader, 5); // sub_layer_profile_idc
                skip_bits(reader, 32); // sub_layer_profile_compatibility_flags
                skip_bits(reader, 1); // sub_layer_progressive_source_flag
                skip_bits(reader, 1); // sub_layer_interlaced_source_flag
                skip_bits(reader, 1); // sub_layer_non_packed_constraint_flag
                skip_bits(reader, 1); // sub_layer_frame_only_constraint_flag
                skip_bits(reader, 44); // sub_layer_reserved_zero_44bits
            }
            if (sub_layer_level_present[index] != 0) {
                skip_bits(reader, 8); // sub_layer_level_idc
            }
        }
    }

    [[nodiscard]] std::optional<sf::h265_sps_qp_info> parse_pps_qp(sf::bitstream_reader &reader) {
        skip_ue(reader); // pps_pic_parameter_set_id
        skip_ue(reader); // pps_seq_parameter_set_id
        skip_bit(reader); // dependent_slice_segments_enabled_flag
        skip_bit(reader); // output_flag_present_flag
        skip_bits(reader, 3); // num_extra_slice_header_bits
        skip_bit(reader); // sign_data_hiding_enabled_flag
        skip_bit(reader); // cabac_init_present_flag
        skip_ue(reader); // num_ref_idx_l0_default_active_minus1
        skip_ue(reader); // num_ref_idx_l1_default_active_minus1

        const auto init_qp_minus26 = read_se(reader);
        const auto base_qp = 26 + init_qp_minus26;
        const bool valid = base_qp >= 0 && base_qp <= 51;

        return sf::h265_sps_qp_info{init_qp_minus26, base_qp, valid};
    }

    [[nodiscard]] std::optional<sf::h265_sps_qp_info> parse_sps_qp(sf::bitstream_reader &reader) {
        skip_bits(reader, 4); // sps_video_parameter_set_id
        const auto sps_max_sub_layers_minus1 = static_cast<uint8_t>(reader.read_bits(3));
        skip_bit(reader); // sps_temporal_id_nesting_flag

        skip_profile_tier_level(reader, sps_max_sub_layers_minus1);

        skip_ue(reader); // sps_seq_parameter_set_id
        const auto chroma_format_idc = reader.read_ue();
        if (chroma_format_idc == 3U) {
            skip_bit(reader); // separate_colour_plane_flag
        }

        skip_ue(reader); // pic_width_in_luma_samples
        skip_ue(reader); // pic_height_in_luma_samples
        if (reader.read_bit() != 0) {
            skip_ue(reader); // conf_win_left_offset
            skip_ue(reader); // conf_win_right_offset
            skip_ue(reader); // conf_win_top_offset
            skip_ue(reader); // conf_win_bottom_offset
        }

        skip_ue(reader); // bit_depth_luma_minus8
        skip_ue(reader); // bit_depth_chroma_minus8
        skip_ue(reader); // log2_max_pic_order_cnt_lsb_minus4

        const auto init_qp_minus26 = read_se(reader);
        const auto base_qp = 26 + init_qp_minus26;
        const bool valid = base_qp >= 0 && base_qp <= 51;

        return sf::h265_sps_qp_info{init_qp_minus26, base_qp, valid};
    }
} // namespace

namespace sf {
    [[nodiscard]] uint8_t get_nal_unit_type(const uint8_t *data, const size_t length) {
        if (!data || length < 2) {
            return 0xFF;
        }

        size_t header_offset = 0;
        if (const auto start_code = find_start_code(data, length); start_code) {
            header_offset = start_code->offset + start_code->size;
        }

        if (header_offset + 1 >= length) {
            return 0xFF;
        }

        const auto byte1 = data[header_offset];
        return static_cast<uint8_t>(byte1 >> 1U & 0x3FU);
    }

    [[nodiscard]] std::optional<h265_sps_qp_info> parse_sps_for_qp(const uint8_t *sps_data, const size_t sps_length) {
        if (!sps_data || sps_length < 2) {
            return std::nullopt;
        }

        size_t header_offset = 0;
        if (const auto start_code = find_start_code(sps_data, sps_length); start_code) {
            header_offset = start_code->offset + start_code->size;
        }

        if (header_offset + 2 > sps_length) {
            return std::nullopt;
        }

        const auto nal_unit_type = get_nal_unit_type(sps_data, sps_length);
        const auto payload_offset = header_offset + 2;
        if (payload_offset >= sps_length) {
            return std::nullopt;
        }

        const auto rbsp = remove_emulation_prevention_bytes(sps_data + payload_offset, sps_length - payload_offset);
        if (rbsp.empty()) {
            return std::nullopt;
        }

        auto reader = bitstream_reader(rbsp);
        if (nal_unit_type == 34) {
            return parse_pps_qp(reader);
        }
        if (nal_unit_type == 33) {
            return parse_sps_qp(reader);
        }

        return std::nullopt;
    }
} // namespace sf
