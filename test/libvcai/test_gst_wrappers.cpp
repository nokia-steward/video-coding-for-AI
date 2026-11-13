// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../common/gst_test_fixture.h"
#include "gst_wrappers.h"

class GstWrappersTest : public GStreamerTestFixture {};

TEST_F(GstWrappersTest, PureHelpersValidateDimensionsPlaneSizesAndBounds) {
    EXPECT_TRUE(sf::is_valid_video_dimensions(1920, 1080));
    EXPECT_FALSE(sf::is_valid_video_dimensions(0, 1080));
    EXPECT_FALSE(sf::is_valid_video_dimensions(1920, 0));
    EXPECT_FALSE(sf::is_valid_video_dimensions(9000, 1080));

    const auto sizes = sf::calculate_yuv420_plane_sizes(4, 4);
    EXPECT_EQ(sizes.luma_size, 16U);
    EXPECT_EQ(sizes.chroma_u_size, 4U);
    EXPECT_EQ(sizes.chroma_v_size, 4U);
    EXPECT_EQ(sizes.total_size, 24U);

    EXPECT_TRUE(sf::is_rectangle_within_bounds(sf::rectangle(2, 2, 4, 4), 16, 16));
    EXPECT_FALSE(sf::is_rectangle_within_bounds(sf::rectangle(-1, 2, 4, 4), 16, 16));
    EXPECT_FALSE(sf::is_rectangle_within_bounds(sf::rectangle(15, 2, 4, 4), 16, 16));
}

TEST_F(GstWrappersTest, PureHelpersHandleOddDimensionsAndEdgeAlignedRectangles) {
    const auto odd_sizes = sf::calculate_yuv420_plane_sizes(3, 3);
    EXPECT_EQ(odd_sizes.luma_size, 9U);
    EXPECT_EQ(odd_sizes.chroma_u_size, 2U);
    EXPECT_EQ(odd_sizes.chroma_v_size, 2U);
    EXPECT_EQ(odd_sizes.total_size, 13U);

    EXPECT_TRUE(sf::is_rectangle_within_bounds(sf::rectangle(12, 12, 4, 4), 16, 16));
    EXPECT_FALSE(sf::is_rectangle_within_bounds(sf::rectangle(12, 12, 5, 4), 16, 16));
    EXPECT_FALSE(sf::is_rectangle_within_bounds(sf::rectangle(12, 12, 4, 5), 16, 16));
}

TEST_F(GstWrappersTest, MapBufferSafeHandlesNullAndValidBuffer) {
    EXPECT_EQ(sf::map_buffer_safe(nullptr, GST_MAP_READ), nullptr);

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 32, nullptr));
    ASSERT_NE(buffer, nullptr);

    auto map_info = sf::map_buffer_safe(buffer.get(), GST_MAP_WRITE);
    ASSERT_NE(map_info, nullptr);
    EXPECT_EQ(map_info->size, 32U);

    gst_buffer_unmap(buffer.get(), map_info.get());
}

TEST_F(GstWrappersTest, CreateVideoInfoSafeRejectsInvalidCapsAndParsesValidCaps) {
    EXPECT_EQ(sf::create_video_info_safe(nullptr), nullptr);

    auto invalid_caps = sf::unique_gst_caps_ptr(gst_caps_from_string("application/x-unknown"));
    ASSERT_NE(invalid_caps, nullptr);
    EXPECT_EQ(sf::create_video_info_safe(invalid_caps.get()), nullptr);

    auto valid_caps = sf::unique_gst_caps_ptr(
            gst_caps_from_string("video/x-raw,format=I420,width=320,height=240,framerate=30/1"));
    ASSERT_NE(valid_caps, nullptr);
    auto video_info = sf::create_video_info_safe(valid_caps.get());
    ASSERT_NE(video_info, nullptr);
    EXPECT_EQ(GST_VIDEO_INFO_WIDTH(video_info.get()), 320);
    EXPECT_EQ(GST_VIDEO_INFO_HEIGHT(video_info.get()), 240);
}

TEST_F(GstWrappersTest, CreateSampleSafeValidatesInputsAndCopiesInfoStructure) {
    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 16, nullptr));
    auto caps = sf::unique_gst_caps_ptr(gst_caps_from_string("video/x-raw,format=I420,width=4,height=4"));
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(caps, nullptr);

    EXPECT_EQ(sf::create_sample_safe(nullptr, caps.get()), nullptr);
    EXPECT_EQ(sf::create_sample_safe(buffer.get(), nullptr), nullptr);

    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    segment.start = 123;
    segment.stop = 456;

    GstStructure *info = gst_structure_new("test-info", "counter", G_TYPE_INT, 7, nullptr);
    ASSERT_NE(info, nullptr);

    auto sample = sf::create_sample_safe(buffer.get(), caps.get(), &segment, info);
    ASSERT_NE(sample, nullptr);

    const GstStructure *sample_info = gst_sample_get_info(sample.get());
    ASSERT_NE(sample_info, nullptr);
    int counter = 0;
    EXPECT_TRUE(gst_structure_get_int(sample_info, "counter", &counter));
    EXPECT_EQ(counter, 7);

    gst_structure_set(info, "counter", G_TYPE_INT, 99, nullptr);
    counter = 0;
    EXPECT_TRUE(gst_structure_get_int(sample_info, "counter", &counter));
    EXPECT_EQ(counter, 7);

    gst_structure_free(info);
}

TEST_F(GstWrappersTest, SetAndExtractBufferTimingRoundTripsTimingValues) {
    sf::unique_gst_buffer_ptr null_buffer(nullptr);
    sf::set_buffer_timing(null_buffer, {1, 2, 3});
    EXPECT_EQ(null_buffer.get(), nullptr);

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 8, nullptr));
    ASSERT_NE(buffer, nullptr);

    const sf::buffer_timing expected{GST_SECOND, GST_SECOND / 2, GST_SECOND / 4};
    sf::set_buffer_timing(buffer, expected);
    const auto actual = sf::extract_buffer_timing(buffer);
    EXPECT_EQ(actual.pts, expected.pts);
    EXPECT_EQ(actual.dts, expected.dts);
    EXPECT_EQ(actual.duration, expected.duration);
}

TEST_F(GstWrappersTest, CreateSampleSafeWithoutInfoKeepsSegmentAndLeavesInfoNull) {
    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 16, nullptr));
    auto caps = sf::unique_gst_caps_ptr(gst_caps_from_string("video/x-raw,format=I420,width=4,height=4"));
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(caps, nullptr);

    GstSegment segment;
    gst_segment_init(&segment, GST_FORMAT_TIME);
    segment.start = GST_SECOND;
    segment.stop = 2 * GST_SECOND;
    segment.time = GST_SECOND / 2;

    auto sample = sf::create_sample_safe(buffer.get(), caps.get(), &segment, nullptr);
    ASSERT_NE(sample, nullptr);

    const GstSegment *sample_segment = gst_sample_get_segment(sample.get());
    ASSERT_NE(sample_segment, nullptr);
    EXPECT_EQ(sample_segment->start, segment.start);
    EXPECT_EQ(sample_segment->stop, segment.stop);
    EXPECT_EQ(sample_segment->time, segment.time);
    EXPECT_EQ(gst_sample_get_info(sample.get()), nullptr);
}

TEST_F(GstWrappersTest, CreateSampleSafeWithNullSegmentStillCreatesSample) {
    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 8, nullptr));
    auto caps = sf::unique_gst_caps_ptr(gst_caps_from_string("video/x-raw,format=I420,width=2,height=2"));
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(caps, nullptr);

    auto sample = sf::create_sample_safe(buffer.get(), caps.get(), nullptr, nullptr);
    ASSERT_NE(sample, nullptr);
    EXPECT_EQ(gst_sample_get_buffer(sample.get()), buffer.get());
    EXPECT_EQ(gst_sample_get_caps(sample.get()), caps.get());
    EXPECT_NE(gst_sample_get_segment(sample.get()), nullptr);
    EXPECT_EQ(gst_sample_get_info(sample.get()), nullptr);
}

TEST_F(GstWrappersTest, MapBufferSafeCanWriteAndReadBackBytes) {
    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 4, nullptr));
    ASSERT_NE(buffer, nullptr);

    auto write_map = sf::map_buffer_safe(buffer.get(), GST_MAP_WRITE);
    ASSERT_NE(write_map, nullptr);
    ASSERT_EQ(write_map->size, 4U);
    write_map->data[0] = 0x11;
    write_map->data[1] = 0x22;
    write_map->data[2] = 0x33;
    write_map->data[3] = 0x44;
    gst_buffer_unmap(buffer.get(), write_map.get());

    auto read_map = sf::map_buffer_safe(buffer.get(), GST_MAP_READ);
    ASSERT_NE(read_map, nullptr);
    ASSERT_EQ(read_map->size, 4U);
    EXPECT_EQ(read_map->data[0], 0x11);
    EXPECT_EQ(read_map->data[1], 0x22);
    EXPECT_EQ(read_map->data[2], 0x33);
    EXPECT_EQ(read_map->data[3], 0x44);
    gst_buffer_unmap(buffer.get(), read_map.get());
}
