// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "bitstream.h"

namespace sf {
    /**
     * @file bitstream.cpp
     * @brief Implementation of the bitstream helper structs.
     */

    void bitstream_writer::write_bits(const uint64_t value, const int num_bits) {
        for (int bit_index = num_bits - 1; bit_index >= 0; --bit_index) {
            const auto bit = static_cast<uint8_t>(value >> bit_index & 1);
            write_bit(bit);
        }
    }

    void bitstream_writer::write_bit(const uint8_t bit) {
        current_byte = static_cast<uint8_t>(current_byte | bit << (7 - bit_position));
        bit_position++;
        if (bit_position == 8) {
            buffer.push_back(current_byte);
            current_byte = 0;
            bit_position = 0;
        }
    }

    void bitstream_writer::write_ue(const unsigned int value) {
        const unsigned int value_to_code = value + 1;
        int leading_zeros = 0;
        unsigned int temp_value = value_to_code;
        while (temp_value > 1) {
            temp_value >>= 1;
            leading_zeros++;
        }

        for (int zero_index = 0; zero_index < leading_zeros; ++zero_index) {
            write_bit(0);
        }
        write_bit(1);
        write_bits(value_to_code, leading_zeros);
    }

    const std::vector<uint8_t> &bitstream_writer::get_buffer() {
        if (bit_position != 0) {
            buffer.push_back(current_byte);
            bit_position = 0;
            current_byte = 0;
        }
        return buffer;
    }

    bitstream_reader::bitstream_reader(const std::vector<uint8_t> &buf) : buffer(buf) {}

    uint64_t bitstream_reader::read_bits(const int num_bits) {
        if (overflow || num_bits < 0 || num_bits > 64) {
            overflow = true;
            return 0;
        }
        uint64_t result = 0;
        for (int bit_index = 0; bit_index < num_bits; ++bit_index) {
            result = result << 1 | read_bit();
        }
        return result;
    }

    uint8_t bitstream_reader::read_bit() {
        if (byte_index >= buffer.size()) {
            overflow = true;
            return 0;
        }
        const auto bit = static_cast<uint8_t>(buffer[byte_index] >> (7 - bit_position) & 1);
        bit_position++;
        if (bit_position == 8) {
            byte_index++;
            bit_position = 0;
        }
        return bit;
    }

    unsigned int bitstream_reader::read_ue() {
        if (overflow) {
            return 0;
        }
        int leading_zeros = 0;
        while (read_bit() == 0U) {
            if (overflow) {
                return 0;
            }
            ++leading_zeros;
            if (leading_zeros > 31) {
                overflow = true;
                return 0;
            }
        }

        if (leading_zeros == 0) {
            return 0;
        }

        const auto value = read_bits(leading_zeros);
        if (overflow) {
            return 0;
        }

        return static_cast<unsigned int>((1U << leading_zeros) - 1 + value);
    }
} // namespace sf
