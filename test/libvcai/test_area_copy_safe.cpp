// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../common/gst_test_fixture.h"
#include "area_copy.h"
#include "gst_raii.h"

namespace {
    struct area_copy_safe_test : GStreamerTestFixture {};
} // namespace

TEST_F(area_copy_safe_test, safe_copy_rejects_mismatched_rectangle_counts) {
    constexpr int width = 4;
    constexpr int height = 4;
    constexpr auto frame_size = static_cast<gsize>(width * height * 3 / 2); // I420

    auto in_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    auto out_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    ASSERT_NE(in_buffer, nullptr);
    ASSERT_NE(out_buffer, nullptr);
    gst_buffer_memset(in_buffer.get(), 0, 42, frame_size);
    gst_buffer_memset(out_buffer.get(), 0, 0, frame_size);

    GstMapInfo in_map{};
    GstMapInfo out_map{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_READ));
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map, GST_MAP_WRITE));
    const sf::unique_gst_buffer_unmapper in_unmapper(in_buffer.get(), &in_map);
    const sf::unique_gst_buffer_unmapper out_unmapper(out_buffer.get(), &out_map);

    const std::vector<sf::rectangle> input_rectangles = {{0, 0, 2, 2}};
    const std::vector<sf::rectangle> packed_rectangles = {};

    const auto result = sf::copy_regions_to_packed_frame_safe(in_map, out_map, width, height, input_rectangles,
                                                              packed_rectangles, sf::video_format::i420);
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error, sf::copy_error::invalid_dimensions);
}

TEST_F(area_copy_safe_test, safe_copy_clamps_boundary_region_without_out_of_bounds_failure) {
    constexpr int width = 4;
    constexpr int height = 4;
    constexpr auto frame_size = static_cast<gsize>(width * height * 3 / 2); // I420

    auto in_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    auto out_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    ASSERT_NE(in_buffer, nullptr);
    ASSERT_NE(out_buffer, nullptr);

    GstMapInfo in_map_write{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map_write, GST_MAP_WRITE));
    for (gsize i = 0; i < frame_size; ++i) {
        in_map_write.data[i] = static_cast<uint8_t>((i + 1) % 251);
    }
    gst_buffer_unmap(in_buffer.get(), &in_map_write);
    gst_buffer_memset(out_buffer.get(), 0, 0, frame_size);

    GstMapInfo in_map{};
    GstMapInfo out_map{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_READ));
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map, GST_MAP_WRITE));
    const sf::unique_gst_buffer_unmapper in_unmapper(in_buffer.get(), &in_map);
    const sf::unique_gst_buffer_unmapper out_unmapper(out_buffer.get(), &out_map);

    const std::vector<sf::rectangle> input_rectangles = {{3, 3, 3, 3}};
    const std::vector<sf::rectangle> packed_rectangles = {{3, 3, 3, 3}};

    const auto result = sf::copy_regions_to_packed_frame_safe(in_map, out_map, width, height, input_rectangles,
                                                              packed_rectangles, sf::video_format::i420);
    ASSERT_TRUE(result.is_success());

    const int luma_index = 3 * width + 3;
    EXPECT_EQ(out_map.data[luma_index], in_map.data[luma_index]);
    EXPECT_EQ(out_map.data[0], 0);
}

TEST_F(area_copy_safe_test, safe_copy_handles_minimal_i420_frame) {
    constexpr int width = 2;
    constexpr int height = 2;
    constexpr auto frame_size = static_cast<gsize>(width * height * 3 / 2); // I420 => 6 bytes

    auto in_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    auto out_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    ASSERT_NE(in_buffer, nullptr);
    ASSERT_NE(out_buffer, nullptr);

    GstMapInfo in_map_write{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map_write, GST_MAP_WRITE));
    for (gsize i = 0; i < frame_size; ++i) {
        in_map_write.data[i] = static_cast<uint8_t>(10 + i);
    }
    gst_buffer_unmap(in_buffer.get(), &in_map_write);
    gst_buffer_memset(out_buffer.get(), 0, 0, frame_size);

    GstMapInfo in_map{};
    GstMapInfo out_map{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_READ));
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map, GST_MAP_WRITE));
    const sf::unique_gst_buffer_unmapper in_unmapper(in_buffer.get(), &in_map);
    const sf::unique_gst_buffer_unmapper out_unmapper(out_buffer.get(), &out_map);

    const std::vector<sf::rectangle> input_rectangles = {{0, 0, 2, 2}};
    const std::vector<sf::rectangle> packed_rectangles = {{0, 0, 2, 2}};

    const auto result = sf::copy_regions_to_packed_frame_safe(in_map, out_map, width, height, input_rectangles,
                                                              packed_rectangles, sf::video_format::i420);
    ASSERT_TRUE(result.is_success());

    for (int i = 0; i < width * height; ++i) {
        EXPECT_EQ(out_map.data[i], in_map.data[i]);
    }
}

TEST_F(area_copy_safe_test, safe_copy_rejects_non_positive_dimensions) {
    constexpr int width = 4;
    constexpr int height = 4;
    constexpr auto frame_size = static_cast<gsize>(width * height * 3 / 2);

    auto in_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    auto out_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    ASSERT_NE(in_buffer, nullptr);
    ASSERT_NE(out_buffer, nullptr);

    GstMapInfo in_map{};
    GstMapInfo out_map{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_READ));
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map, GST_MAP_WRITE));
    const sf::unique_gst_buffer_unmapper in_unmapper(in_buffer.get(), &in_map);
    const sf::unique_gst_buffer_unmapper out_unmapper(out_buffer.get(), &out_map);

    const std::vector<sf::rectangle> rects = {{0, 0, 2, 2}};
    const auto result =
            sf::copy_regions_to_packed_frame_safe(in_map, out_map, 0, height, rects, rects, sf::video_format::i420);
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error, sf::copy_error::invalid_dimensions);
}

TEST_F(area_copy_safe_test, safe_copy_rejects_unsupported_format) {
    constexpr int width = 4;
    constexpr int height = 4;
    constexpr auto frame_size = static_cast<gsize>(width * height * 3 / 2);

    auto in_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    auto out_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    ASSERT_NE(in_buffer, nullptr);
    ASSERT_NE(out_buffer, nullptr);

    GstMapInfo in_map{};
    GstMapInfo out_map{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_READ));
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map, GST_MAP_WRITE));
    const sf::unique_gst_buffer_unmapper in_unmapper(in_buffer.get(), &in_map);
    const sf::unique_gst_buffer_unmapper out_unmapper(out_buffer.get(), &out_map);

    const std::vector<sf::rectangle> rects = {{0, 0, 2, 2}};
    const auto result =
            sf::copy_regions_to_packed_frame_safe(in_map, out_map, width, height, rects, rects, sf::video_format::nv12);
    EXPECT_FALSE(result.is_success());
    EXPECT_EQ(result.error, sf::copy_error::unsupported_format);
}
