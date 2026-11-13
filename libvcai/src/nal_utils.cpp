// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "nal_utils.h"

#include <climits>
#include <cstdint>
#include <gst/gst.h>
#include <vector>

#include "../include/logging.h"
#include "bitstream.h"
#include "packed_regions_info.h"

// Debug category for NAL unit utilities.
GST_DEBUG_CATEGORY_STATIC(nal_utils);
#define GST_CAT_DEFAULT nal_utils

/**
 * @brief Initialize the NAL utils debug category at load time.
 */
static bool debug_category_initialized = [] {
    GST_DEBUG_CATEGORY_INIT(nal_utils, "nal-utils", 0, "NAL unit utilities");
    return true;
}();

/**
 * @brief Initialize the nal_utils debug category.
 * This function can be called explicitly to ensure the category is initialized after gst_init().
 */
void init_nal_utils_debug_category() { GST_DEBUG_CATEGORY_INIT(nal_utils, "nal-utils", 0, "NAL unit utilities"); }

namespace sf {
    /**
     * @file nal_utils.cpp
     * @brief Implementation of NAL unit creation and injection logic.
     */

    namespace {
        /**
         * @struct nal_info
         * @brief Holds the parsed type and layer ID of a NAL unit.
         */
        struct nal_info {
            int type = -1;
            int layer_id = -1;
        };

        /**
         * @brief Writes a variable-length integer according to the HEVC SEI spec.
         *
         * Writes one or more bytes to the bitstream. If a value is >= 255, a byte
         * of 0xFF is written and 255 is subtracted from the value. This repeats
         * until the value is < 255, at which point the final value is written.
         *
         * @param writer The bitstream_writer to write to.
         * @param value The integer value to encode.
         */
        void write_sei_variable_int(bitstream_writer &writer, const uint32_t value) {
            uint32_t remaining_value = value;
            while (remaining_value >= 255) {
                writer.write_bits(0xFF, 8);
                remaining_value -= 255;
            }
            writer.write_bits(static_cast<uint8_t>(remaining_value), 8);
        }

        /**
         * @brief Converts a Raw Byte Sequence Payload (RBSP) to a NAL unit payload.
         *
         * This function inserts emulation prevention bytes (0x03) into the RBSP data to prevent sequences
         * that could be misinterpreted as NAL unit start codes.
         *
         * @param rbsp The input vector containing the RBSP data.
         * @return A new vector containing the NAL unit payload with emulation prevention bytes.
         */
        [[nodiscard]] std::vector<uint8_t> rbsp_to_nal_payload(const std::vector<uint8_t> &rbsp) {
            auto nal_payload = std::vector<uint8_t>();
            nal_payload.reserve(rbsp.size());
            for (size_t byte_index = 0; byte_index < rbsp.size(); ++byte_index) {
                if (byte_index + 2 < rbsp.size() && rbsp[byte_index] == 0x00 && rbsp[byte_index + 1] == 0x00 &&
                    rbsp[byte_index + 2] <= 0x03) {
                    nal_payload.push_back(0x00);
                    nal_payload.push_back(0x00);
                    nal_payload.push_back(0x03);
                    nal_payload.push_back(rbsp[byte_index + 2]);
                    byte_index += 2;
                } else {
                    nal_payload.push_back(rbsp[byte_index]);
                }
            }
            return nal_payload;
        }

        /**
         * @brief Finds the position of the next start code in a buffer.
         * @param buffer The raw buffer to search.
         * @param length The total length of the buffer.
         * @param start_offset The offset from which to begin the search.
         * @return The offset of the start code, or SIZE_MAX if not found.
         */
        [[nodiscard]] size_t find_next_start_code(const uint8_t *const buffer, const size_t length,
                                                  const size_t start_offset) {
            for (size_t byte_index = start_offset; byte_index + 3 < length; ++byte_index) {
                if (buffer[byte_index] == 0x00 && buffer[byte_index + 1] == 0x00) {
                    if (buffer[byte_index + 2] == 0x01 ||
                        (byte_index + 3 < length && buffer[byte_index + 2] == 0x00 && buffer[byte_index + 3] == 0x01)) {
                        return byte_index;
                    }
                }
            }
            return SIZE_MAX;
        }

        /**
         * @brief Gets the H.265 NAL unit type and layer ID from a raw buffer at a given offset.
         * @param buffer The raw buffer containing the NAL unit data.
         * @param length The total length of the raw buffer.
         * @param offset The offset to the beginning of the NAL unit's start code.
         * @return A nal_info struct containing the type and layer_id.
         */
        [[nodiscard]] nal_info get_nal_info(const uint8_t *const buffer, const size_t length, const size_t offset) {
            size_t header_start = 0;
            if (offset + 2 < length && buffer[offset] == 0x00 && buffer[offset + 1] == 0x00) {
                if (buffer[offset + 2] == 0x01) {
                    header_start = offset + 3;
                } else if (offset + 3 < length && buffer[offset + 2] == 0x00 && buffer[offset + 3] == 0x01) {
                    header_start = offset + 4;
                } else {
                    return {};
                }
            } else {
                return {};
            }

            if (header_start + 1 >= length) {
                return {};
            }

            const auto byte1 = buffer[header_start];
            const auto byte2 = buffer[header_start + 1];

            auto info = nal_info{};
            info.type = byte1 >> 1 & 0x3F;
            info.layer_id = (byte1 & 0x01) << 5 | byte2 >> 3;
            return info;
        }
    } // namespace

    [[nodiscard]] std::vector<uint8_t> create_sei_nal_unit(const packed_regions_info &pri_data) {
        const auto pri_payload = write_packed_regions_info(pri_data);
        return create_sei_nal_unit(226, pri_payload);
    }

    [[nodiscard]] std::vector<uint8_t> create_sei_nal_unit(const uint32_t payload_type,
                                                           const std::vector<uint8_t> &payload_bytes) {
        auto sei_writer = bitstream_writer();

        write_sei_variable_int(sei_writer, payload_type);
        write_sei_variable_int(sei_writer, static_cast<uint32_t>(payload_bytes.size()));

        for (const auto byte: payload_bytes) {
            sei_writer.write_bits(byte, 8);
        }
        sei_writer.write_bit(1); // rbsp_stop_one_bit
        const auto sei_rbsp = sei_writer.get_buffer();

        auto nal_unit = std::vector<uint8_t>();
        // NAL Header for PREFIX_SEI_NUT (39), layer_id=0, temporal_id_plus1=1
        nal_unit.push_back(0x4E);
        nal_unit.push_back(0x01);
        const auto nal_payload = rbsp_to_nal_payload(sei_rbsp);
        nal_unit.insert(nal_unit.end(), nal_payload.begin(), nal_payload.end());

        return nal_unit;
    }

    [[nodiscard]] bool contains_hevc_irap_vcl_nal(const uint8_t *const frame_data, const size_t frame_length) {
        if (!frame_data || frame_length == 0) {
            return false;
        }

        size_t current_pos = find_next_start_code(frame_data, frame_length, 0);
        while (current_pos != SIZE_MAX) {
            const auto info = get_nal_info(frame_data, frame_length, current_pos);
            const bool is_vcl = info.type >= 0 && info.type <= 31;
            const bool is_layer_zero = info.layer_id == 0;
            const bool is_irap = info.type >= 16 && info.type <= 21;
            if (is_vcl && is_layer_zero && is_irap) {
                return true;
            }
            current_pos = find_next_start_code(frame_data, frame_length, current_pos + 1);
        }
        return false;
    }

    [[nodiscard]] std::vector<uint8_t> inject_sei_message(const uint8_t *const frame_data, const size_t frame_length,
                                                          const std::vector<uint8_t> &sei_nal_unit) {
        size_t injection_point = frame_length;

        size_t current_pos = find_next_start_code(frame_data, frame_length, 0);
        while (current_pos != SIZE_MAX) {
            const auto info = get_nal_info(frame_data, frame_length, current_pos);
            // VCL NAL units are types 0-31
            if (const bool is_vcl = info.type >= 0 && info.type <= 31; is_vcl && info.layer_id == 0) {
                injection_point = current_pos;
                break;
            }
            current_pos = find_next_start_code(frame_data, frame_length, current_pos + 1);
        }

        if (injection_point == frame_length) {
            warning(nal_utils, "No VCL NAL unit found. Appending SEI to the end of the frame.");
        }

        auto final_buffer = std::vector<uint8_t>();
        const size_t sei_with_start_code_size = 3 + sei_nal_unit.size();
        final_buffer.reserve(frame_length + sei_with_start_code_size);

        final_buffer.insert(final_buffer.end(), frame_data, frame_data + injection_point);
        final_buffer.push_back(0x00);
        final_buffer.push_back(0x00);
        final_buffer.push_back(0x01);
        final_buffer.insert(final_buffer.end(), sei_nal_unit.begin(), sei_nal_unit.end());
        final_buffer.insert(final_buffer.end(), frame_data + injection_point, frame_data + frame_length);

        return final_buffer;
    }
} // namespace sf
