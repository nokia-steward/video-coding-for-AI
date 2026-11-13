// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <vector>

#include "../../libvcai/include/bitstream.h"
#include "../../libvcai/include/h265_sps_parser.h"

namespace {
    void write_se(sf::bitstream_writer &writer, const int32_t value) {
        const uint32_t code_num = value <= 0 ? static_cast<uint32_t>(-value * 2) : static_cast<uint32_t>(value * 2 - 1);
        writer.write_ue(code_num);
    }

    [[nodiscard]] std::vector<uint8_t> build_test_pps_nal(const int32_t init_qp_minus26) {
        sf::bitstream_writer writer;
        writer.write_ue(0); // pps_pic_parameter_set_id
        writer.write_ue(0); // pps_seq_parameter_set_id
        writer.write_bit(0); // dependent_slice_segments_enabled_flag
        writer.write_bit(0); // output_flag_present_flag
        writer.write_bits(0, 3); // num_extra_slice_header_bits
        writer.write_bit(0); // sign_data_hiding_enabled_flag
        writer.write_bit(0); // cabac_init_present_flag
        writer.write_ue(0); // num_ref_idx_l0_default_active_minus1
        writer.write_ue(0); // num_ref_idx_l1_default_active_minus1
        write_se(writer, init_qp_minus26); // init_qp_minus26
        writer.write_bit(0); // constrained_intra_pred_flag
        writer.write_bit(0); // transform_skip_enabled_flag
        writer.write_bit(0); // cu_qp_delta_enabled_flag
        write_se(writer, 0); // pps_cb_qp_offset
        write_se(writer, 0); // pps_cr_qp_offset
        writer.write_bit(0); // pps_slice_chroma_qp_offsets_present_flag
        writer.write_bit(0); // weighted_pred_flag
        writer.write_bit(0); // weighted_bipred_flag
        writer.write_bit(0); // transquant_bypass_enabled_flag
        writer.write_bit(0); // tiles_enabled_flag
        writer.write_bit(0); // entropy_coding_sync_enabled_flag
        writer.write_bit(0); // pps_loop_filter_across_slices_enabled_flag
        writer.write_bit(0); // deblocking_filter_control_present_flag
        writer.write_bit(0); // pps_scaling_list_data_present_flag
        writer.write_bit(0); // lists_modification_present_flag
        writer.write_ue(0); // log2_parallel_merge_level_minus2
        writer.write_bit(0); // slice_segment_header_extension_present_flag
        writer.write_bit(0); // pps_extension_present_flag
        writer.write_bit(1); // rbsp_stop_one_bit

        const auto payload = writer.get_buffer();
        std::vector<uint8_t> nal;
        nal.reserve(3 + 2 + payload.size());
        nal.push_back(0x00);
        nal.push_back(0x00);
        nal.push_back(0x01);
        nal.push_back(0x44); // PPS nal_unit_type=34, nuh_layer_id=0
        nal.push_back(0x01); // nuh_temporal_id_plus1=1
        nal.insert(nal.end(), payload.begin(), payload.end());
        return nal;
    }
} // namespace

TEST(h265_sps_parser, parse_qp_from_pps) {
    const auto nal = build_test_pps_nal(6);
    const auto qp_info = sf::parse_sps_for_qp(nal.data(), nal.size());

    ASSERT_TRUE(qp_info.has_value());
    EXPECT_TRUE(qp_info->valid);
    EXPECT_EQ(qp_info->init_qp_minus26, 6);
    EXPECT_EQ(qp_info->base_qp, 32);
}

TEST(h265_sps_parser, nal_unit_type_parsing) {
    const auto nal = build_test_pps_nal(0);
    const auto nal_type = sf::get_nal_unit_type(nal.data(), nal.size());
    EXPECT_EQ(nal_type, 34);
}
