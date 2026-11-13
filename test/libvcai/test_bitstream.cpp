// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include "bitstream.h"

// Test fixture for bitstream tests
struct bitstream_test : ::testing::Test {};

// Test case for round-trip serialization and deserialization
TEST_F(bitstream_test, round_trip) {
    auto writer = sf::bitstream_writer();

    // Write a variety of values
    writer.write_bits(0x1, 1);
    writer.write_ue(42);
    writer.write_bits(0b1010, 4);
    writer.write_ue(0);
    writer.write_bits(0xDEADBEEF, 32);
    writer.write_ue(255);

    const auto buffer = writer.get_buffer();
    auto reader = sf::bitstream_reader(buffer);

    // Read the values back and verify
    ASSERT_EQ(reader.read_bits(1), 0x1);
    ASSERT_EQ(reader.read_ue(), 42);
    ASSERT_EQ(reader.read_bits(4), 0b1010);
    ASSERT_EQ(reader.read_ue(), 0);
    ASSERT_EQ(reader.read_bits(32), 0xDEADBEEF);
    ASSERT_EQ(reader.read_ue(), 255);
}

// Test case for non-byte-aligned data
TEST_F(bitstream_test, non_byte_aligned) {
    auto writer = sf::bitstream_writer();

    // Write 3 bits, then 5 bits, then 8 bits
    writer.write_bits(0b101, 3);
    writer.write_bits(0b11001, 5);
    writer.write_bits(0xAB, 8);

    const auto buffer = writer.get_buffer();
    auto reader = sf::bitstream_reader(buffer);

    // Read the values back and verify
    ASSERT_EQ(reader.read_bits(3), 0b101);
    ASSERT_EQ(reader.read_bits(5), 0b11001);
    ASSERT_EQ(reader.read_bits(8), 0xAB);
}

// Test case for writing a single bit
TEST_F(bitstream_test, single_bit) {
    auto writer = sf::bitstream_writer();
    writer.write_bit(1);
    writer.write_bit(0);
    writer.write_bit(1);

    const auto buffer = writer.get_buffer();
    auto reader = sf::bitstream_reader(buffer);

    ASSERT_EQ(reader.read_bit(), 1);
    ASSERT_EQ(reader.read_bit(), 0);
    ASSERT_EQ(reader.read_bit(), 1);
}

// Test case for Exp-Golomb encoding of zero
TEST_F(bitstream_test, exp_golomb_zero) {
    auto writer = sf::bitstream_writer();
    writer.write_ue(0);

    const auto buffer = writer.get_buffer();
    ASSERT_EQ(buffer.size(), 1);
    ASSERT_EQ(buffer[0], 0x80); // "1" in MSB, padded with 7 zeros

    auto reader = sf::bitstream_reader(buffer);
    ASSERT_EQ(reader.read_ue(), 0);
}

TEST_F(bitstream_test, read_past_end_sets_overflow) {
    std::vector<uint8_t> buffer = {0x80};
    auto reader = sf::bitstream_reader(buffer);
    for (int i = 0; i < 8; ++i) {
        (void) reader.read_bit();
    }
    ASSERT_FALSE(reader.has_overflow());
    (void) reader.read_bit();
    ASSERT_TRUE(reader.has_overflow());
}
