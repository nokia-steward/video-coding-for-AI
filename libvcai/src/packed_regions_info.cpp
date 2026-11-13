// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "packed_regions_info.h"
#include <sstream>
#include "bitstream.h"

namespace sf {
    [[nodiscard]] std::string to_string(const resampling_ratio &r) {
        std::stringstream ss;
        ss << "ResamplingRatio(width_num: " << r.width_num_minus1 + 1 << ", width_den: " << r.width_denom_minus1 + 1
           << ", fixed_aspect: " << std::boolalpha << r.fixed_aspect_ratio_flag;
        if (!r.fixed_aspect_ratio_flag) {
            ss << ", height_num: " << r.height_num_minus1.value_or(0)
               << ", height_den: " << r.height_denom_minus1.value_or(0);
        }
        ss << ")";
        return ss.str();
    }

    [[nodiscard]] std::string to_string(const region &r) {
        std::stringstream ss;
        ss << "Region(id: " << r.id.value_or(0) << ", layer_id: " << r.layer_id.value_or(0)
           << ", is_a_layer_flag: " << r.is_a_layer_flag.value_or(false) << ", packed_xy: ("
           << r.top_left_in_units_x.value_or(0) << ", " << r.top_left_in_units_y.value_or(0) << "), packed_wh-1: ("
           << r.width_in_units_minus1.value_or(0) << ", " << r.height_in_units_minus1.value_or(0)
           << "), ratio_idx: " << r.resampling_ratio_idx.value_or(0) << ", target_xy: ("
           << r.target_region_top_left_in_units_x.value_or(0) << ", " << r.target_region_top_left_in_units_y.value_or(0)
           << "))";
        return ss.str();
    }

    [[nodiscard]] std::string to_string(const packed_regions_info &pri) {
        std::stringstream ss;
        ss << "PackedRegionsInfo:\n"
           << "  cancel_flag: " << std::boolalpha << pri.cancel_flag << "\n"
           << "  persistence_flag: " << pri.persistence_flag.value_or(false) << "\n"
           << "  multilayer_flag: " << pri.multilayer_flag.value_or(false) << "\n"
           << "  use_max_dimensions_flag: " << pri.use_max_dimensions_flag.value_or(false) << "\n"
           << "  log2_unit_size: " << static_cast<int>(pri.log2_unit_size.value_or(0)) << "\n"
           << "  region_size_len_minus1: " << static_cast<int>(pri.region_size_len_minus1.value_or(0)) << "\n"
           << "  region_id_present_flag: " << pri.region_id_present_flag.value_or(false) << "\n"
           << "  target_pic_params_present_flag: " << pri.target_pic_params_present_flag.value_or(false) << "\n"
           << "  target_pic_width_minus1: " << pri.target_pic_width_minus1.value_or(0) << "\n"
           << "  target_pic_height_minus1: " << pri.target_pic_height_minus1.value_or(0) << "\n"
           << "  Inferred Resampling Ratio (idx 0): " << to_string(pri.inferred_resampling_ratio) << "\n"
           << "  Signaled Resampling Ratios (" << pri.resampling_ratios.size() << "):\n";
        for (size_t signaled_ratio_index = 0; signaled_ratio_index < pri.resampling_ratios.size();
             ++signaled_ratio_index) {
            ss << "    [" << signaled_ratio_index + 1 << "] " << to_string(pri.resampling_ratios[signaled_ratio_index])
               << "\n";
        }
        ss << "  Regions (" << pri.regions.size() << "):\n";
        for (const auto &region: pri.regions) {
            ss << "    " << to_string(region) << "\n";
        }
        return ss.str();
    }


    /**
     * @file packed_regions_info.cpp
     * @brief Implementation of the serialization and deserialization functions.
     */

    namespace {
        /**
         * @brief Compute ceil(log2(number)) for positive integers.
         * @param number Input value.
         * @return Smallest integer k such that 2^k >= number.
         */
        [[nodiscard]] unsigned int ceil_log2(const unsigned int number) {
            if (number <= 1) {
                return 0;
            }
            // Use a manual implementation to avoid compiler warnings
            unsigned int result = 0;
            unsigned int temp = number - 1;
            while (temp > 0) {
                temp >>= 1;
                result++;
            }
            return result;
        }

        /**
         * @brief Writes resampling ratios to the bitstream writer.
         * @param writer Bitstream writer.
         * @param pri_data Packed regions info data.
         */
        void write_resampling_ratios(bitstream_writer &writer, const packed_regions_info &pri_data) {
            writer.write_ue(static_cast<unsigned int>(pri_data.resampling_ratios.size()));

            for (const auto &ratio: pri_data.resampling_ratios) {
                writer.write_ue(ratio.width_num_minus1);
                writer.write_ue(ratio.width_denom_minus1);
                writer.write_bits(ratio.fixed_aspect_ratio_flag, 1);
                if (!ratio.fixed_aspect_ratio_flag) {
                    writer.write_ue(ratio.height_num_minus1.value());
                    writer.write_ue(ratio.height_denom_minus1.value());
                }
            }
        }

        /**
         * @brief Writes a single region to the bitstream writer.
         * @param writer Bitstream writer.
         * @param region Region to write.
         * @param pri_data Packed regions info data for flags and sizes.
         */
        void write_region(bitstream_writer &writer, const region &reg, const packed_regions_info &pri_data) {
            if (pri_data.region_id_present_flag.value()) {
                writer.write_ue(reg.id.value());
            }
            if (pri_data.multilayer_flag.value()) {
                writer.write_ue(reg.layer_id.value());
                writer.write_bits(reg.is_a_layer_flag.value(), 1);
            }

            if (!reg.is_a_layer_flag.value_or(false)) {
                const int region_size_len = pri_data.region_size_len_minus1.value() + 1;
                writer.write_bits(reg.top_left_in_units_x.value(), region_size_len);
                writer.write_bits(reg.top_left_in_units_y.value(), region_size_len);
                writer.write_bits(reg.width_in_units_minus1.value(), region_size_len);
                writer.write_bits(reg.height_in_units_minus1.value(), region_size_len);
            }

            if (!pri_data.resampling_ratios.empty()) {
                const int num_bits =
                        static_cast<int>(ceil_log2(static_cast<unsigned int>(pri_data.resampling_ratios.size() + 1)));
                if (num_bits > 0) {
                    writer.write_bits(reg.resampling_ratio_idx.value_or(0), num_bits);
                }
            }

            if (pri_data.target_pic_params_present_flag.value()) {
                const int region_size_len = pri_data.region_size_len_minus1.value() + 1;
                writer.write_bits(reg.target_region_top_left_in_units_x.value(), region_size_len);
                writer.write_bits(reg.target_region_top_left_in_units_y.value(), region_size_len);
            }
        }

        /**
         * @struct parsed_resampling_ratios
         * @brief Holds parsed PRI resampling ratios and syntax metadata.
         */
        struct parsed_resampling_ratios {
            unsigned int pri_num_resampling_ratios_minus1{};
            resampling_ratio inferred_resampling_ratio{0U, 0U, true, std::nullopt, std::nullopt};
            std::vector<resampling_ratio> signaled_resampling_ratios{};
        };

        /**
         * @brief Reads resampling ratios from the bitstream reader.
         * @param reader Bitstream reader.
         * @return Parsed inferred and signaled resampling ratios.
         */
        [[nodiscard]] parsed_resampling_ratios read_resampling_ratios(bitstream_reader &reader) {
            auto parsed = parsed_resampling_ratios{};
            parsed.pri_num_resampling_ratios_minus1 = reader.read_ue();
            parsed.signaled_resampling_ratios.resize(parsed.pri_num_resampling_ratios_minus1);

            for (size_t ratio_index = 0; ratio_index < parsed.signaled_resampling_ratios.size(); ++ratio_index) {
                auto &ratio = parsed.signaled_resampling_ratios[ratio_index];
                ratio.width_num_minus1 = reader.read_ue();
                ratio.width_denom_minus1 = reader.read_ue();
                ratio.fixed_aspect_ratio_flag = reader.read_bits(1);
                if (!ratio.fixed_aspect_ratio_flag) {
                    ratio.height_num_minus1 = reader.read_ue();
                    ratio.height_denom_minus1 = reader.read_ue();
                }
            }

            return parsed;
        }

        /**
         * @brief Reads a single region from the bitstream reader.
         * @param reader Bitstream reader.
         * @param pri_data Packed regions info data for flags and sizes.
         * @param pri_num_resampling_ratios_minus1 PRI syntax value.
         * @return Read region.
         */
        [[nodiscard]] region read_region(bitstream_reader &reader, const packed_regions_info &pri_data,
                                         const unsigned int pri_num_resampling_ratios_minus1) {
            region reg;

            if (pri_data.region_id_present_flag.value()) {
                reg.id = reader.read_ue();
            }
            if (pri_data.multilayer_flag.value()) {
                reg.layer_id = reader.read_ue();
                reg.is_a_layer_flag = reader.read_bits(1);
            }

            if (!reg.is_a_layer_flag.value_or(false)) {
                const int region_size_len = pri_data.region_size_len_minus1.value() + 1;
                reg.top_left_in_units_x = reader.read_bits(region_size_len);
                reg.top_left_in_units_y = reader.read_bits(region_size_len);
                reg.width_in_units_minus1 = reader.read_bits(region_size_len);
                reg.height_in_units_minus1 = reader.read_bits(region_size_len);
            }

            if (const int num_bits = static_cast<int>(ceil_log2(pri_num_resampling_ratios_minus1 + 1)); num_bits > 0) {
                reg.resampling_ratio_idx = reader.read_bits(num_bits);
            } else {
                reg.resampling_ratio_idx = 0; // Inferred
            }

            if (pri_data.target_pic_params_present_flag.value()) {
                const int region_size_len = pri_data.region_size_len_minus1.value() + 1;
                reg.target_region_top_left_in_units_x = reader.read_bits(region_size_len);
                reg.target_region_top_left_in_units_y = reader.read_bits(region_size_len);
            }

            return reg;
        }
    } // namespace

    [[nodiscard]] std::vector<uint8_t> write_packed_regions_info(const packed_regions_info &data) {
        auto writer = bitstream_writer();

        writer.write_bits(data.cancel_flag, 1);

        if (!data.cancel_flag) {
            writer.write_bits(data.persistence_flag.value(), 1);
            writer.write_ue(static_cast<unsigned int>(data.regions.size() - 1));
            writer.write_bits(data.multilayer_flag.value(), 1);
            writer.write_bits(data.use_max_dimensions_flag.value(), 1);
            writer.write_bits(data.log2_unit_size.value(), 4);
            writer.write_bits(data.region_size_len_minus1.value(), 4);
            writer.write_bits(data.region_id_present_flag.value(), 1);
            writer.write_bits(data.target_pic_params_present_flag.value(), 1);

            if (data.target_pic_params_present_flag.value()) {
                writer.write_bits(data.target_pic_width_minus1.value(), 16);
                writer.write_bits(data.target_pic_height_minus1.value(), 16);
            }

            write_resampling_ratios(writer, data);

            for (const auto &reg: data.regions) {
                write_region(writer, reg, data);
            }
        }
        return writer.get_buffer();
    }

    [[nodiscard]] packed_regions_info read_packed_regions_info(const std::vector<uint8_t> &buffer, bool *parse_ok_out) {
        auto reader = bitstream_reader(buffer);
        auto data = packed_regions_info();

        data.cancel_flag = reader.read_bits(1);

        if (!data.cancel_flag) {
            data.persistence_flag = reader.read_bits(1);
            const auto num_regions_minus1 = reader.read_ue();
            data.multilayer_flag = reader.read_bits(1);
            data.use_max_dimensions_flag = reader.read_bits(1);
            data.log2_unit_size = reader.read_bits(4);
            data.region_size_len_minus1 = reader.read_bits(4);
            data.region_id_present_flag = reader.read_bits(1);
            data.target_pic_params_present_flag = reader.read_bits(1);

            if (data.target_pic_params_present_flag.value()) {
                data.target_pic_width_minus1 = reader.read_bits(16);
                data.target_pic_height_minus1 = reader.read_bits(16);
            }

            const auto parsed_ratios = read_resampling_ratios(reader);
            data.inferred_resampling_ratio = parsed_ratios.inferred_resampling_ratio;
            data.resampling_ratios = parsed_ratios.signaled_resampling_ratios;

            const unsigned int num_regions = num_regions_minus1 + 1;
            data.regions.resize(num_regions);
            for (size_t region_index = 0; region_index < data.regions.size(); ++region_index) {
                data.regions[region_index] = read_region(reader, data, parsed_ratios.pri_num_resampling_ratios_minus1);
            }
        }
        if (parse_ok_out != nullptr) {
            *parse_ok_out = !reader.has_overflow();
        }
        return data;
    }
} // namespace sf
