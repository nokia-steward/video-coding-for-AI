// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sf {
    /**
     * @file encoder_optimization_info.h
     * @brief Public API for serializing and deserializing Encoder Optimization Information SEI messages.
     */

    /**
     * @struct encoder_optimization_info
     * @brief Represents the H.274 8.31 Encoder Optimization Information payload.
     */
    struct encoder_optimization_info {
        static constexpr uint16_t type_object_based = 0x01;
        static constexpr uint16_t type_temporal_resampling = 0x02;
        static constexpr uint16_t type_spatial_resampling = 0x04;
        static constexpr uint16_t type_temporal_quality = 0x08;
        static constexpr uint16_t type_spatial_quality = 0x10;
        static constexpr uint16_t type_privacy_protection = 0x20;
        static constexpr uint16_t type_luma_adaptation = 0x40;

        bool cancel_flag{false};
        std::optional<bool> persistence_flag{};
        std::optional<uint8_t> for_human_viewing_idc{};
        std::optional<uint8_t> for_machine_analysis_idc{};
        std::optional<uint8_t> reserved_zero_2bits{};
        std::optional<uint16_t> type{};

        std::optional<uint16_t> object_based_idc{};
        std::optional<unsigned int> quant_threshold_delta{};
        std::optional<bool> pic_quant_object_flag{};

        std::optional<bool> temporal_resampling_type_flag{};
        std::optional<unsigned int> num_int_pics{};
        std::optional<bool> src_pic_flag{};

        std::optional<bool> orig_pic_dimensions_flag{};
        std::optional<uint16_t> orig_pic_width_minus1{};
        std::optional<uint16_t> orig_pic_height_minus1{};
        std::optional<uint8_t> spatial_hor_resampling_type_idc{};
        std::optional<uint8_t> spatial_ver_resampling_type_idc{};

        std::optional<uint16_t> privacy_protection_method_idc{};
        std::optional<uint8_t> privacy_info_type{};

        // EoiLumaAdaptation syntax elements (JVET-AN2032, H.274 8.31 extension).
        std::optional<unsigned int> luma_adaptation_idc{};
        std::optional<uint8_t> ratio_luma_value{};
        std::optional<bool> ratio_luma_sign_flag{};
        std::optional<uint8_t> display_model{};
        std::optional<uint8_t> backscale_ratio_value{};
    };

    /**
     * @brief Creates a string representation of an encoder_optimization_info object.
     * @param eoi The object to format.
     * @return A string representation of the object.
     */
    [[nodiscard]] std::string to_string(const encoder_optimization_info &eoi);

    /**
     * @brief Serializes an encoder_optimization_info struct into a byte buffer.
     * @param data The struct to serialize.
     * @return A vector of bytes representing the serialized data.
     */
    [[nodiscard]] std::vector<uint8_t> write_encoder_optimization_info(const encoder_optimization_info &data);

    /**
     * @brief Deserializes a byte buffer into an encoder_optimization_info struct.
     * @param buffer The byte buffer to read from.
     * @return The deserialized encoder_optimization_info struct.
     */
    [[nodiscard]] encoder_optimization_info read_encoder_optimization_info(const std::vector<uint8_t> &buffer);
} // namespace sf
