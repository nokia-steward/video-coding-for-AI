// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_optimization_info.h"

#include <sstream>
#include <stdexcept>

#include "bitstream.h"

namespace sf {
    namespace {
        [[nodiscard]] bool has_type_flag(const uint16_t eoi_type, const uint16_t mask) {
            return (eoi_type & mask) != 0;
        }

        template<typename T>
        [[nodiscard]] const T &required(const std::optional<T> &value, const char *field_name) {
            if (!value.has_value()) {
                throw std::runtime_error(std::string("Missing required EOI field: ") + field_name);
            }
            return value.value();
        }
    } // namespace

    [[nodiscard]] std::string to_string(const encoder_optimization_info &eoi) {
        std::stringstream ss;
        ss << "EncoderOptimizationInfo:\n"
           << "  cancel_flag: " << std::boolalpha << eoi.cancel_flag << "\n";

        if (eoi.cancel_flag) {
            return ss.str();
        }

        ss << "  persistence_flag: " << eoi.persistence_flag.value_or(false) << "\n"
           << "  for_human_viewing_idc: " << static_cast<int>(eoi.for_human_viewing_idc.value_or(0)) << "\n"
           << "  for_machine_analysis_idc: " << static_cast<int>(eoi.for_machine_analysis_idc.value_or(0)) << "\n"
           << "  reserved_zero_2bits: " << static_cast<int>(eoi.reserved_zero_2bits.value_or(0)) << "\n"
           << "  type: 0x" << std::hex << eoi.type.value_or(0) << std::dec << "\n";

        if (eoi.object_based_idc.has_value()) {
            ss << "  object_based_idc: 0x" << std::hex << eoi.object_based_idc.value() << std::dec << "\n";
        }
        if (eoi.quant_threshold_delta.has_value()) {
            ss << "  quant_threshold_delta: " << eoi.quant_threshold_delta.value() << "\n";
        }
        if (eoi.pic_quant_object_flag.has_value()) {
            ss << "  pic_quant_object_flag: " << eoi.pic_quant_object_flag.value() << "\n";
        }
        if (eoi.temporal_resampling_type_flag.has_value()) {
            ss << "  temporal_resampling_type_flag: " << eoi.temporal_resampling_type_flag.value() << "\n";
        }
        if (eoi.num_int_pics.has_value()) {
            ss << "  num_int_pics: " << eoi.num_int_pics.value() << "\n";
        }
        if (eoi.src_pic_flag.has_value()) {
            ss << "  src_pic_flag: " << eoi.src_pic_flag.value() << "\n";
        }
        if (eoi.orig_pic_dimensions_flag.has_value()) {
            ss << "  orig_pic_dimensions_flag: " << eoi.orig_pic_dimensions_flag.value() << "\n";
        }
        if (eoi.orig_pic_width_minus1.has_value()) {
            ss << "  orig_pic_width_minus1: " << eoi.orig_pic_width_minus1.value() << "\n";
        }
        if (eoi.orig_pic_height_minus1.has_value()) {
            ss << "  orig_pic_height_minus1: " << eoi.orig_pic_height_minus1.value() << "\n";
        }
        if (eoi.spatial_hor_resampling_type_idc.has_value()) {
            ss << "  spatial_hor_resampling_type_idc: " << static_cast<int>(eoi.spatial_hor_resampling_type_idc.value())
               << "\n";
        }
        if (eoi.spatial_ver_resampling_type_idc.has_value()) {
            ss << "  spatial_ver_resampling_type_idc: " << static_cast<int>(eoi.spatial_ver_resampling_type_idc.value())
               << "\n";
        }
        if (eoi.privacy_protection_method_idc.has_value()) {
            ss << "  privacy_protection_method_idc: " << eoi.privacy_protection_method_idc.value() << "\n";
        }
        if (eoi.privacy_info_type.has_value()) {
            ss << "  privacy_info_type: " << static_cast<int>(eoi.privacy_info_type.value()) << "\n";
        }
        if (eoi.luma_adaptation_idc.has_value()) {
            ss << "  luma_adaptation_idc: " << eoi.luma_adaptation_idc.value() << "\n";
        }
        if (eoi.ratio_luma_value.has_value()) {
            ss << "  ratio_luma_value: " << static_cast<int>(eoi.ratio_luma_value.value()) << "\n";
        }
        if (eoi.ratio_luma_sign_flag.has_value()) {
            ss << "  ratio_luma_sign_flag: " << eoi.ratio_luma_sign_flag.value() << "\n";
        }
        if (eoi.display_model.has_value()) {
            ss << "  display_model: " << static_cast<int>(eoi.display_model.value()) << "\n";
        }
        if (eoi.backscale_ratio_value.has_value()) {
            ss << "  backscale_ratio_value: " << static_cast<int>(eoi.backscale_ratio_value.value()) << "\n";
        }

        return ss.str();
    }

    [[nodiscard]] std::vector<uint8_t> write_encoder_optimization_info(const encoder_optimization_info &data) {
        auto writer = bitstream_writer();

        writer.write_bits(data.cancel_flag, 1);

        if (!data.cancel_flag) {
            const auto persistence_flag = required(data.persistence_flag, "persistence_flag");
            const auto for_human_viewing_idc = required(data.for_human_viewing_idc, "for_human_viewing_idc");
            const auto for_machine_analysis_idc = required(data.for_machine_analysis_idc, "for_machine_analysis_idc");
            const auto reserved_zero_2bits = required(data.reserved_zero_2bits, "reserved_zero_2bits");
            const auto eoi_type = required(data.type, "type");

            writer.write_bits(persistence_flag, 1);
            writer.write_bits(for_human_viewing_idc, 2);
            writer.write_bits(for_machine_analysis_idc, 2);
            writer.write_bits(reserved_zero_2bits, 2);
            writer.write_bits(eoi_type, 16);

            if (has_type_flag(eoi_type, encoder_optimization_info::type_object_based)) {
                const auto object_based_idc = required(data.object_based_idc, "object_based_idc");
                writer.write_bits(object_based_idc, 16);

                if ((object_based_idc & 0x02) != 0) {
                    const auto quant_threshold_delta = required(data.quant_threshold_delta, "quant_threshold_delta");
                    writer.write_ue(quant_threshold_delta);

                    if (quant_threshold_delta > 0) {
                        writer.write_bits(required(data.pic_quant_object_flag, "pic_quant_object_flag"), 1);
                    }
                }
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_temporal_resampling)) {
                const auto temporal_resampling_type_flag =
                        required(data.temporal_resampling_type_flag, "temporal_resampling_type_flag");
                const auto num_int_pics = required(data.num_int_pics, "num_int_pics");

                writer.write_bits(temporal_resampling_type_flag, 1);
                writer.write_ue(num_int_pics);

                if (temporal_resampling_type_flag && num_int_pics > 0) {
                    writer.write_bits(required(data.src_pic_flag, "src_pic_flag"), 1);
                }
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_spatial_resampling)) {
                const auto orig_pic_dimensions_flag =
                        required(data.orig_pic_dimensions_flag, "orig_pic_dimensions_flag");
                writer.write_bits(orig_pic_dimensions_flag, 1);

                if (orig_pic_dimensions_flag) {
                    writer.write_bits(required(data.orig_pic_width_minus1, "orig_pic_width_minus1"), 16);
                    writer.write_bits(required(data.orig_pic_height_minus1, "orig_pic_height_minus1"), 16);
                } else {
                    writer.write_bits(required(data.spatial_hor_resampling_type_idc, "spatial_hor_resampling_type_idc"),
                                      2);
                    writer.write_bits(required(data.spatial_ver_resampling_type_idc, "spatial_ver_resampling_type_idc"),
                                      2);
                }
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_privacy_protection)) {
                writer.write_bits(required(data.privacy_protection_method_idc, "privacy_protection_method_idc"), 16);
                writer.write_bits(required(data.privacy_info_type, "privacy_info_type"), 8);
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_luma_adaptation)) {
                const auto luma_adaptation_idc = required(data.luma_adaptation_idc, "luma_adaptation_idc");
                writer.write_ue(luma_adaptation_idc);
                writer.write_bits(required(data.ratio_luma_value, "ratio_luma_value"), 7);
                writer.write_bits(required(data.ratio_luma_sign_flag, "ratio_luma_sign_flag"), 1);

                if (luma_adaptation_idc == 0U) {
                    writer.write_bits(required(data.display_model, "display_model"), 4);
                }

                writer.write_bits(required(data.backscale_ratio_value, "backscale_ratio_value"), 7);
            }
        }

        return writer.get_buffer();
    }

    [[nodiscard]] encoder_optimization_info read_encoder_optimization_info(const std::vector<uint8_t> &buffer) {
        auto reader = bitstream_reader(buffer);
        auto data = encoder_optimization_info{};

        data.cancel_flag = reader.read_bits(1);

        if (!data.cancel_flag) {
            data.persistence_flag = static_cast<bool>(reader.read_bits(1));
            data.for_human_viewing_idc = static_cast<uint8_t>(reader.read_bits(2));
            data.for_machine_analysis_idc = static_cast<uint8_t>(reader.read_bits(2));
            data.reserved_zero_2bits = static_cast<uint8_t>(reader.read_bits(2));
            data.type = static_cast<uint16_t>(reader.read_bits(16));

            const uint16_t eoi_type = data.type.value_or(0);

            if (has_type_flag(eoi_type, encoder_optimization_info::type_object_based)) {
                data.object_based_idc = static_cast<uint16_t>(reader.read_bits(16));
                if ((data.object_based_idc.value() & 0x02) != 0) {
                    data.quant_threshold_delta = reader.read_ue();
                    if (data.quant_threshold_delta.value() > 0) {
                        data.pic_quant_object_flag = static_cast<bool>(reader.read_bits(1));
                    }
                }
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_temporal_resampling)) {
                data.temporal_resampling_type_flag = static_cast<bool>(reader.read_bits(1));
                data.num_int_pics = reader.read_ue();

                if (data.temporal_resampling_type_flag.value() && data.num_int_pics.value() > 0) {
                    data.src_pic_flag = static_cast<bool>(reader.read_bits(1));
                }
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_spatial_resampling)) {
                data.orig_pic_dimensions_flag = static_cast<bool>(reader.read_bits(1));

                if (data.orig_pic_dimensions_flag.value()) {
                    data.orig_pic_width_minus1 = static_cast<uint16_t>(reader.read_bits(16));
                    data.orig_pic_height_minus1 = static_cast<uint16_t>(reader.read_bits(16));
                } else {
                    data.spatial_hor_resampling_type_idc = static_cast<uint8_t>(reader.read_bits(2));
                    data.spatial_ver_resampling_type_idc = static_cast<uint8_t>(reader.read_bits(2));
                }
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_privacy_protection)) {
                data.privacy_protection_method_idc = static_cast<uint16_t>(reader.read_bits(16));
                data.privacy_info_type = static_cast<uint8_t>(reader.read_bits(8));
            }

            if (has_type_flag(eoi_type, encoder_optimization_info::type_luma_adaptation)) {
                data.luma_adaptation_idc = reader.read_ue();
                data.ratio_luma_value = static_cast<uint8_t>(reader.read_bits(7));
                data.ratio_luma_sign_flag = static_cast<bool>(reader.read_bits(1));

                if (data.luma_adaptation_idc.value() == 0U) {
                    data.display_model = static_cast<uint8_t>(reader.read_bits(4));
                }

                data.backscale_ratio_value = static_cast<uint8_t>(reader.read_bits(7));
            }
        }

        return data;
    }
} // namespace sf
