// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace sf {
    /**
     * @file h265_sps_parser.h
     * @brief Parses H.265 SPS/PPS data to extract QP information.
     */

    /**
     * @struct h265_sps_qp_info
     * @brief Holds QP data extracted from SPS/PPS.
     */
    struct h265_sps_qp_info {
        int init_qp_minus26; ///< Raw init_qp_minus26 value from SPS/PPS.
        int base_qp; ///< Computed base QP (26 + init_qp_minus26).
        bool valid; ///< True when the parsed values are in a valid range.
    };

    /**
     * @brief Parse an SPS/PPS NAL unit to extract QP-related parameters.
     * @param sps_data Pointer to the SPS/PPS NAL unit (may include start code).
     * @param sps_length Length of the NAL unit buffer in bytes.
     * @return Parsed QP info if successful, otherwise std::nullopt.
     */
    [[nodiscard]] std::optional<h265_sps_qp_info> parse_sps_for_qp(const uint8_t *sps_data, size_t sps_length);

    /**
     * @brief Extract the H.265 NAL unit type from a buffer.
     * @param data Pointer to NAL unit data (with or without start code).
     * @param length Length of the buffer in bytes.
     * @return The NAL unit type, or 0xFF on failure.
     */
    [[nodiscard]] uint8_t get_nal_unit_type(const uint8_t *data, size_t length);
} // namespace sf
