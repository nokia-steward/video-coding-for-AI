// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf {
    /**
     * @file bitstream.h
     * @brief Provides helper structs for bit-level reading and writing of a binary stream.
     */

    /**
     * @struct bitstream_writer
     * @brief A helper struct to write data bit-by-bit into a byte vector.
     *
     * This struct follows functional programming principles:
     * - Encapsulated internal mutation: state changes are contained within the struct
     * - Pure interface: methods have clear input/output relationships
     * - RAII: automatic resource management through destructor
     * - Immutable external interface: get_buffer() returns const reference
     */
    struct bitstream_writer {
        /**
         * @brief Default constructor. Initializes all member variables.
         */
        bitstream_writer() : buffer(), current_byte(0), bit_position(0) {}

        /**
         * @brief Writes a specified number of bits from a value.
         * @param value The value to write bits from.
         * @param num_bits The number of bits to write (from the LSB of the value).
         */
        void write_bits(uint64_t value, int num_bits);

        /**
         * @brief Writes a single bit to the stream.
         * @param bit The bit to write (must be 0 or 1).
         */
        void write_bit(uint8_t bit);

        /**
         * @brief Writes an unsigned Exp-Golomb coded value.
         * @param value The unsigned integer to encode.
         */
        void write_ue(unsigned int value);

        /**
         * @brief Flushes any remaining bits in the current byte and returns the buffer.
         * @note This method is not const as it may modify the buffer by flushing the last byte.
         * @return A const reference to the internal byte buffer.
         */
        [[nodiscard]] const std::vector<uint8_t> &get_buffer();

    private:
        std::vector<uint8_t> buffer;
        uint8_t current_byte;
        int bit_position;
    };

    /**
     * @struct bitstream_reader
     * @brief A helper struct to read data bit-by-bit from a byte buffer.
     *
     * This struct follows functional programming principles:
     * - Immutable buffer: only reads from the provided buffer
     * - Overflow flag: underrun sets @ref has_overflow; callers should check after bounded reads
     */
    struct bitstream_reader {
        /**
         * @brief Constructs a bitstream_reader with a reference to a data buffer.
         * @param buffer The byte buffer to read from.
         */
        explicit bitstream_reader(const std::vector<uint8_t> &buffer);

        /**
         * @brief Reads a specified number of bits from the stream.
         * @param num_bits The number of bits to read.
         * @return The read value as a 64-bit unsigned integer.
         */
        [[nodiscard]] uint64_t read_bits(int num_bits);

        /**
         * @brief Reads a single bit from the stream.
         * @return The read bit as a uint8_t (0 or 1).
         */
        [[nodiscard]] uint8_t read_bit();

        /**
         * @brief Reads an unsigned Exp-Golomb coded value from the stream.
         * @return The decoded unsigned integer.
         */
        [[nodiscard]] unsigned int read_ue();

        /**
         * @brief True if a read ran past the end of the buffer (including mid-read in read_bits / read_ue).
         */
        [[nodiscard]] bool has_overflow() const { return overflow; }

    private:
        const std::vector<uint8_t> &buffer;
        size_t byte_index = 0;
        int bit_position = 0;
        bool overflow = false;
    };
} // namespace sf
