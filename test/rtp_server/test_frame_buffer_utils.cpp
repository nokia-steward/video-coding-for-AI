// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cstring>
#include <gtest/gtest.h>

#include "../../rtp_server/src/pipeline/frame_buffer_utils.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    struct frame_buffer_utils_test : GStreamerTestFixtureRtpServer {};

    // Helper function to create a buffer with specific data
    [[nodiscard]] sf::unique_gst_buffer_ptr create_test_buffer(const guint8 *data, gsize size) {
        auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, size, nullptr));
        if (!buffer) {
            return nullptr;
        }

        GstMapInfo map_info;
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_WRITE)) {
            return nullptr;
        }

        std::memcpy(map_info.data, data, size);
        gst_buffer_unmap(buffer.get(), &map_info);

        return buffer;
    }

    // Helper function to get buffer data
    [[nodiscard]] std::vector<guint8> get_buffer_data(const sf::unique_gst_buffer_ptr &buffer) {
        if (!buffer) {
            return {};
        }

        GstMapInfo map_info;
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_READ)) {
            return {};
        }

        std::vector<guint8> data(map_info.data, map_info.data + map_info.size);
        gst_buffer_unmap(buffer.get(), &map_info);
        return data;
    }

    // Test: Buffer with existing 3-byte start code (0x00 0x00 0x01)
    TEST_F(frame_buffer_utils_test, buffer_with_3_byte_start_code) {
        const guint8 test_data[] = {0x00, 0x00, 0x01, 0x42, 0x43, 0x44};
        auto buffer = create_test_buffer(test_data, sizeof(test_data));

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        ASSERT_EQ(result_data.size(), sizeof(test_data));
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x01);
        EXPECT_EQ(result_data[3], 0x42);
    }

    // Test: Buffer with existing 4-byte start code (0x00 0x00 0x00 0x01)
    TEST_F(frame_buffer_utils_test, buffer_with_4_byte_start_code) {
        const guint8 test_data[] = {0x00, 0x00, 0x00, 0x01, 0x42, 0x43, 0x44};
        auto buffer = create_test_buffer(test_data, sizeof(test_data));

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        ASSERT_EQ(result_data.size(), sizeof(test_data));
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x00);
        EXPECT_EQ(result_data[3], 0x01);
    }

    // Test: Buffer without start code - should prepend 4-byte start code
    TEST_F(frame_buffer_utils_test, buffer_without_start_code) {
        const guint8 test_data[] = {0x42, 0x43, 0x44, 0x45};
        auto buffer = create_test_buffer(test_data, sizeof(test_data));

        // Set PTS for timing preservation test
        GST_BUFFER_PTS(buffer.get()) = 1000;
        GST_BUFFER_DTS(buffer.get()) = 2000;
        GST_BUFFER_DURATION(buffer.get()) = 33333;

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        ASSERT_EQ(result_data.size(), sizeof(test_data) + 4);

        // Check start code was prepended
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x00);
        EXPECT_EQ(result_data[3], 0x01);

        // Check original data follows
        EXPECT_EQ(result_data[4], 0x42);
        EXPECT_EQ(result_data[5], 0x43);
        EXPECT_EQ(result_data[6], 0x44);
        EXPECT_EQ(result_data[7], 0x45);

        // Check timing information was preserved
        EXPECT_EQ(GST_BUFFER_PTS(result.get()), 1000);
        EXPECT_EQ(GST_BUFFER_DTS(result.get()), 2000);
        EXPECT_EQ(GST_BUFFER_DURATION(result.get()), 33333);
    }

    // Test: Empty buffer (size 0)
    TEST_F(frame_buffer_utils_test, empty_buffer) {
        auto buffer = create_test_buffer(nullptr, 0);

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        // Empty buffer should get start code added
        ASSERT_EQ(result_data.size(), 4);
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x00);
        EXPECT_EQ(result_data[3], 0x01);
    }

    // Test: Buffer too small for start code check (1 byte)
    TEST_F(frame_buffer_utils_test, buffer_too_small_1_byte) {
        const guint8 test_data[] = {0x42};
        auto buffer = create_test_buffer(test_data, sizeof(test_data));

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        ASSERT_EQ(result_data.size(), sizeof(test_data) + 4);
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x00);
        EXPECT_EQ(result_data[3], 0x01);
        EXPECT_EQ(result_data[4], 0x42);
    }

    // Test: Buffer too small for start code check (2 bytes)
    TEST_F(frame_buffer_utils_test, buffer_too_small_2_bytes) {
        const guint8 test_data[] = {0x42, 0x43};
        auto buffer = create_test_buffer(test_data, sizeof(test_data));

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        ASSERT_EQ(result_data.size(), sizeof(test_data) + 4);
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x00);
        EXPECT_EQ(result_data[3], 0x01);
        EXPECT_EQ(result_data[4], 0x42);
        EXPECT_EQ(result_data[5], 0x43);
    }

    // Test: Buffer with partial start code (0x00 0x00 but not 0x01)
    TEST_F(frame_buffer_utils_test, buffer_with_partial_start_code) {
        const guint8 test_data[] = {0x00, 0x00, 0x42, 0x43};
        auto buffer = create_test_buffer(test_data, sizeof(test_data));

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        // Should add start code since it's not a valid start code
        ASSERT_EQ(result_data.size(), sizeof(test_data) + 4);
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x00);
        EXPECT_EQ(result_data[3], 0x01);
    }

    // Test: Null buffer input
    TEST_F(frame_buffer_utils_test, null_buffer_input) {
        sf::unique_gst_buffer_ptr buffer(nullptr);

        const auto result = add_h265_start_code(buffer);

        EXPECT_EQ(result, nullptr);
    }

    // Test: Timing information preservation
    TEST_F(frame_buffer_utils_test, timing_information_preserved) {
        const guint8 test_data[] = {0x42, 0x43, 0x44};
        auto buffer = create_test_buffer(test_data, sizeof(test_data));

        // Set various timing fields
        GST_BUFFER_PTS(buffer.get()) = 12345;
        GST_BUFFER_DTS(buffer.get()) = 23456;
        GST_BUFFER_DURATION(buffer.get()) = 33333;
        GST_BUFFER_OFFSET(buffer.get()) = 100;
        GST_BUFFER_OFFSET_END(buffer.get()) = 200;
        GST_BUFFER_FLAGS(buffer.get()) = GST_BUFFER_FLAG_DELTA_UNIT;

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        EXPECT_EQ(GST_BUFFER_PTS(result.get()), 12345);
        EXPECT_EQ(GST_BUFFER_DTS(result.get()), 23456);
        EXPECT_EQ(GST_BUFFER_DURATION(result.get()), 33333);
        EXPECT_EQ(GST_BUFFER_OFFSET(result.get()), 100);
        EXPECT_EQ(GST_BUFFER_OFFSET_END(result.get()), 200);
        EXPECT_EQ(GST_BUFFER_FLAGS(result.get()), GST_BUFFER_FLAG_DELTA_UNIT);
    }

    // Test: Large buffer without start code
    TEST_F(frame_buffer_utils_test, large_buffer_without_start_code) {
        constexpr gsize large_size = 1024;
        std::vector<guint8> test_data(large_size, 0xAA);
        auto buffer = create_test_buffer(test_data.data(), large_size);

        const auto result = add_h265_start_code(buffer);

        ASSERT_NE(result, nullptr);
        const auto result_data = get_buffer_data(result);
        ASSERT_EQ(result_data.size(), large_size + 4);
        EXPECT_EQ(result_data[0], 0x00);
        EXPECT_EQ(result_data[1], 0x00);
        EXPECT_EQ(result_data[2], 0x00);
        EXPECT_EQ(result_data[3], 0x01);
        // Check original data preserved
        EXPECT_EQ(result_data[4], 0xAA);
        EXPECT_EQ(result_data[result_data.size() - 1], 0xAA);
    }
} // namespace
