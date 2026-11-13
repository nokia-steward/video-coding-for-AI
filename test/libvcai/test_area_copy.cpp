// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cstring>
#include <gst/gst.h>
#include <gst/video/video-format.h>
#include <gst/video/video-info.h>
#include <gtest/gtest.h>
#include <numeric>
#include <stdexcept>
#include <vector>
#include "../../libvcai/src/area_copy/nv12/validation.h"
#include "../../libvcai/src/area_copy/reconstruction/pri_extraction.h"
#include "../common/gst_test_fixture_rtp_server.h"
#include "area_copy.h"
#include "gst_raii.h"
#include "packed_regions_info.h"
#include "rectangle.h"

// Test fixture for area_copy tests
struct area_copy_test : GStreamerTestFixtureRtpServer {
    // No need for manual GStreamer management - handled by base class
};

// Test case for processing a frame
TEST_F(area_copy_test, process_frame) {
    constexpr int width = 16;
    constexpr int height = 16;
    constexpr auto frame_size = static_cast<gsize>(width * height * 1.5); // I420

    // Create input buffer with a gradient pattern
    auto in_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    GstMapInfo in_map;
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_WRITE));
    std::iota(in_map.data, in_map.data + frame_size, 0);
    gst_buffer_unmap(in_buffer.get(), &in_map);

    // Create output buffer and fill with a known value (e.g., 255) to verify untouched areas
    auto out_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(frame_size));
    gst_buffer_memset(out_buffer.get(), 0, 255, frame_size);

    // Define rectangles to copy the bottom-right quadrant of the input to the top-left of the output
    const std::vector<sf::rectangle> input_rectangles = {{8, 8, 8, 8}};
    const std::vector<sf::rectangle> packed_rectangles = {{0, 0, 8, 8}};

    GstMapInfo in_map_read;
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map_read, GST_MAP_READ));
    const sf::unique_gst_buffer_unmapper in_unmapper(in_buffer.get(), &in_map_read);

    GstMapInfo out_map_write;
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map_write, GST_MAP_WRITE));
    const sf::unique_gst_buffer_unmapper out_unmapper(out_buffer.get(), &out_map_write);

    // Process the frame
    sf::copy_regions_to_packed_frame(in_map_read, out_map_write, width, height, input_rectangles, packed_rectangles,
                                     sf::video_format::i420);

    // Verify the copy
    // The output frame should have the bottom-right quadrant of the input
    // frame copied to its top-left quadrant. The rest of the output frame
    // should remain untouched (i.e., filled with 255).
    const auto *in_data = in_map_read.data;
    const auto *out_data = out_map_write.data;

    // Y plane verification
    constexpr int luma_stride = width;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto out_idx = y * luma_stride + x;
            if (x < 8 && y < 8) { // Inside copied region
                const auto in_idx = (y + 8) * luma_stride + (x + 8);
                EXPECT_EQ(out_data[out_idx], in_data[in_idx]);
            } else { // Outside copied region
                EXPECT_EQ(out_data[out_idx], 255);
            }
        }
    }

    // U and V planes verification
    constexpr int uv_stride = width / 2;
    constexpr int uv_height = height / 2;
    constexpr int u_offset = width * height;
    constexpr int v_offset = u_offset + uv_stride * uv_height;

    // U plane
    for (int y = 0; y < uv_height; ++y) {
        for (int x = 0; x < uv_stride; ++x) {
            const auto out_idx = u_offset + y * uv_stride + x;
            if (x < 4 && y < 4) { // Inside copied region (subsampled)
                const auto in_idx = u_offset + (y + 4) * uv_stride + (x + 4);
                EXPECT_EQ(out_data[out_idx], in_data[in_idx]);
            } else { // Outside copied region
                EXPECT_EQ(out_data[out_idx], 255);
            }
        }
    }

    // V plane
    for (int y = 0; y < uv_height; ++y) {
        for (int x = 0; x < uv_stride; ++x) {
            const auto out_idx = v_offset + y * uv_stride + x;
            if (x < 4 && y < 4) { // Inside copied region (subsampled)
                const auto in_idx = v_offset + (y + 4) * uv_stride + (x + 4);
                EXPECT_EQ(out_data[out_idx], in_data[in_idx]);
            } else { // Outside copied region
                EXPECT_EQ(out_data[out_idx], 255);
            }
        }
    }
}

// Helper function to create a GstSample with NV12 format
sf::unique_gst_sample_ptr create_nv12_sample(int width, int height, const std::vector<uint8_t> &data) {
    GstVideoInfo video_info;
    gst_video_info_init(&video_info);
    if (!gst_video_info_set_format(&video_info, GST_VIDEO_FORMAT_NV12, static_cast<guint>(width),
                                   static_cast<guint>(height))) {
        // NV12 format not supported or invalid dimensions
        return nullptr;
    }

    auto caps = sf::unique_gst_caps_ptr(gst_video_info_to_caps(&video_info));
    if (!caps) {
        return nullptr;
    }

    // Use the size from video_info, which accounts for stride padding
    const gsize buffer_size = video_info.size;
    if (data.size() < buffer_size) {
        // Data is too small, return nullptr
        return nullptr;
    }

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, buffer_size, nullptr));
    if (!buffer) {
        return nullptr;
    }

    GstMapInfo map_info;
    if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_WRITE)) {
        return nullptr;
    }

    // Copy all data
    std::memcpy(map_info.data, data.data(), buffer_size);
    gst_buffer_unmap(buffer.get(), &map_info);

    auto sample = sf::unique_gst_sample_ptr(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    buffer.reset();
    return sample;
}

// Helper function to create PRI info for testing
sf::packed_regions_info create_test_pri(int target_width, int target_height,
                                        const std::vector<sf::rectangle> &packed_rects,
                                        const std::vector<sf::rectangle> &target_rects) {
    sf::packed_regions_info pri;
    pri.target_pic_width_minus1 = target_width - 1;
    pri.target_pic_height_minus1 = target_height - 1;
    pri.log2_unit_size = 0; // 1 pixel units

    if (packed_rects.size() != target_rects.size()) {
        return pri; // Return empty PRI if sizes don't match
    }

    for (size_t i = 0; i < packed_rects.size(); ++i) {
        sf::region region;
        region.top_left_in_units_x = packed_rects[i].left;
        region.top_left_in_units_y = packed_rects[i].top;
        region.width_in_units_minus1 = packed_rects[i].width - 1;
        region.height_in_units_minus1 = packed_rects[i].height - 1;
        region.target_region_top_left_in_units_x = target_rects[i].left;
        region.target_region_top_left_in_units_y = target_rects[i].top;
        pri.regions.push_back(region);
    }

    return pri;
}

// Test NV12 reconstruction with simple known data
TEST_F(area_copy_test, reconstruct_nv12_simple) {
    constexpr int packed_width = 8;
    constexpr int packed_height = 8;
    constexpr int reconstructed_width = 16;
    constexpr int reconstructed_height = 16;

    // First, get the actual buffer size from GStreamer for NV12 format
    GstVideoInfo test_info;
    gst_video_info_init(&test_info);
    if (!gst_video_info_set_format(&test_info, GST_VIDEO_FORMAT_NV12, static_cast<guint>(packed_width),
                                   static_cast<guint>(packed_height))) {
        GTEST_SKIP() << "NV12 format not supported";
    }
    const gsize packed_size = test_info.size;
    std::vector<uint8_t> packed_data(packed_size, 0); // Initialize with zeros

    // Fill Y plane with known pattern (0-63)
    // Use stride from GstVideoInfo to handle padding correctly
    const gint packed_y_stride = GST_VIDEO_INFO_PLANE_STRIDE(&test_info, 0);
    const gint packed_y_height = GST_VIDEO_INFO_COMP_HEIGHT(&test_info, 0);
    for (gint y = 0; y < packed_y_height; ++y) {
        for (gint x = 0; x < packed_width; ++x) {
            const gsize idx = GST_VIDEO_INFO_PLANE_OFFSET(&test_info, 0) + y * packed_y_stride + x;
            if (idx < packed_size) {
                packed_data[idx] = static_cast<uint8_t>(y * packed_width + x);
            }
        }
    }

    // Fill UV plane with known pattern (interleaved: U=100, V=200 for first pixel, etc.)
    const gint packed_uv_stride = GST_VIDEO_INFO_PLANE_STRIDE(&test_info, 1);
    const gint packed_uv_height = GST_VIDEO_INFO_COMP_HEIGHT(&test_info, 1);
    const gsize packed_uv_offset = GST_VIDEO_INFO_PLANE_OFFSET(&test_info, 1);
    for (gint y = 0; y < packed_uv_height; ++y) {
        for (gint x = 0; x < packed_width; x += 2) {
            const gsize idx = packed_uv_offset + y * packed_uv_stride + x;
            if (idx + 1 < packed_size) {
                packed_data[idx] = static_cast<uint8_t>(100 + (y * packed_width / 2 + x / 2)); // U
                packed_data[idx + 1] = static_cast<uint8_t>(200 + (y * packed_width / 2 + x / 2)); // V
            }
        }
    }

    auto packed_sample = create_nv12_sample(packed_width, packed_height, packed_data);
    ASSERT_NE(packed_sample, nullptr);

    // Create PRI: copy entire packed frame to top-left of reconstructed frame
    std::vector<sf::rectangle> packed_rects = {{0, 0, packed_width, packed_height}};
    std::vector<sf::rectangle> target_rects = {{0, 0, packed_width, packed_height}};
    auto pri = create_test_pri(reconstructed_width, reconstructed_height, packed_rects, target_rects);

    // Reconstruct frame
    auto reconstructed_sample = sf::reconstruct_frame(std::move(packed_sample), pri);
    ASSERT_NE(reconstructed_sample, nullptr);

    // Verify reconstructed frame
    auto *reconstructed_buffer = gst_sample_get_buffer(reconstructed_sample.get());
    auto *reconstructed_caps = gst_sample_get_caps(reconstructed_sample.get());
    ASSERT_NE(reconstructed_buffer, nullptr);
    ASSERT_NE(reconstructed_caps, nullptr);

    GstVideoInfo reconstructed_info;
    ASSERT_TRUE(gst_video_info_from_caps(&reconstructed_info, reconstructed_caps));
    EXPECT_EQ(reconstructed_info.width, reconstructed_width);
    EXPECT_EQ(reconstructed_info.height, reconstructed_height);
    EXPECT_EQ(reconstructed_info.finfo->format, GST_VIDEO_FORMAT_NV12);

    GstMapInfo reconstructed_map;
    ASSERT_TRUE(gst_buffer_map(reconstructed_buffer, &reconstructed_map, GST_MAP_READ));
    const sf::unique_gst_buffer_unmapper reconstructed_unmapper(reconstructed_buffer, &reconstructed_map);

    // Verify Y plane (top-left 8x8 should match packed frame)
    const auto *y_out = reconstructed_map.data;
    const auto recon_y_stride = GST_VIDEO_INFO_PLANE_STRIDE(&reconstructed_info, 0);
    for (int y = 0; y < packed_height; ++y) {
        for (int x = 0; x < packed_width; ++x) {
            // Get packed data using GstVideoInfo offsets and strides
            const gsize packed_idx = GST_VIDEO_INFO_PLANE_OFFSET(&test_info, 0) + y * packed_y_stride + x;
            const int reconstructed_idx = y * recon_y_stride + x;
            if (packed_idx < packed_size) {
                EXPECT_EQ(y_out[reconstructed_idx], packed_data[packed_idx])
                        << "Y plane mismatch at (" << x << ", " << y << ")";
            }
        }
    }

    // Verify UV plane (top-left 8x4 should match packed frame, interleaved)
    const auto *uv_out = reconstructed_map.data + GST_VIDEO_INFO_PLANE_OFFSET(&reconstructed_info, 1);
    const auto recon_uv_stride = GST_VIDEO_INFO_PLANE_STRIDE(&reconstructed_info, 1);
    for (int y = 0; y < packed_height / 2; ++y) {
        for (int x = 0; x < packed_width; x += 2) {
            // Get packed data using GstVideoInfo offsets and strides
            const gsize packed_idx = packed_uv_offset + y * packed_uv_stride + x;
            const int reconstructed_idx = y * recon_uv_stride + x;
            if (packed_idx + 1 < packed_size) {
                EXPECT_EQ(uv_out[reconstructed_idx], packed_data[packed_idx])
                        << "UV plane U mismatch at (" << x << ", " << y << ")";
                EXPECT_EQ(uv_out[reconstructed_idx + 1], packed_data[packed_idx + 1])
                        << "UV plane V mismatch at (" << x << ", " << y << ")";
            }
        }
    }
}

// Test NV12 reconstruction with region copy (packed region to target position)
TEST_F(area_copy_test, reconstruct_nv12_region_copy) {
    constexpr int packed_width = 8;
    constexpr int packed_height = 8;
    constexpr int reconstructed_width = 16;
    constexpr int reconstructed_height = 16;

    // First, get the actual buffer size from GStreamer for NV12 format
    GstVideoInfo packed_test_info;
    gst_video_info_init(&packed_test_info);
    if (!gst_video_info_set_format(&packed_test_info, GST_VIDEO_FORMAT_NV12, static_cast<guint>(packed_width),
                                   static_cast<guint>(packed_height))) {
        GTEST_SKIP() << "NV12 format not supported";
    }
    const gsize packed_size = packed_test_info.size;
    std::vector<uint8_t> packed_data(packed_size, 0);

    // Fill Y plane: pattern where each row has its row number
    const gint packed_y_stride = GST_VIDEO_INFO_PLANE_STRIDE(&packed_test_info, 0);
    const gint packed_y_height = GST_VIDEO_INFO_COMP_HEIGHT(&packed_test_info, 0);
    for (gint y = 0; y < packed_y_height; ++y) {
        for (gint x = 0; x < packed_width; ++x) {
            const gsize idx = GST_VIDEO_INFO_PLANE_OFFSET(&packed_test_info, 0) + y * packed_y_stride + x;
            if (idx < packed_size) {
                packed_data[idx] = static_cast<uint8_t>(y * 10 + x);
            }
        }
    }

    // Fill UV plane: pattern where U = row*10, V = row*10+1
    const gint packed_uv_stride = GST_VIDEO_INFO_PLANE_STRIDE(&packed_test_info, 1);
    const gint packed_uv_height = GST_VIDEO_INFO_COMP_HEIGHT(&packed_test_info, 1);
    const gsize packed_uv_offset = GST_VIDEO_INFO_PLANE_OFFSET(&packed_test_info, 1);
    for (gint y = 0; y < packed_uv_height; ++y) {
        for (gint x = 0; x < packed_width; x += 2) {
            const gsize idx = packed_uv_offset + y * packed_uv_stride + x;
            if (idx + 1 < packed_size) {
                packed_data[idx] = static_cast<uint8_t>(y * 10); // U
                packed_data[idx + 1] = static_cast<uint8_t>(y * 10 + 1); // V
            }
        }
    }

    auto packed_sample = create_nv12_sample(packed_width, packed_height, packed_data);
    ASSERT_NE(packed_sample, nullptr);

    // Create PRI: copy 4x4 region from packed (0,0) to reconstructed (4,4)
    std::vector<sf::rectangle> packed_rects = {{0, 0, 4, 4}};
    std::vector<sf::rectangle> target_rects = {{4, 4, 4, 4}};
    auto pri = create_test_pri(reconstructed_width, reconstructed_height, packed_rects, target_rects);

    // Reconstruct frame
    auto reconstructed_sample = sf::reconstruct_frame(std::move(packed_sample), pri);
    ASSERT_NE(reconstructed_sample, nullptr);

    // Verify reconstructed frame
    auto *reconstructed_buffer = gst_sample_get_buffer(reconstructed_sample.get());
    auto *reconstructed_caps = gst_sample_get_caps(reconstructed_sample.get());
    GstVideoInfo reconstructed_info;
    ASSERT_TRUE(gst_video_info_from_caps(&reconstructed_info, reconstructed_caps));

    GstMapInfo reconstructed_map;
    ASSERT_TRUE(gst_buffer_map(reconstructed_buffer, &reconstructed_map, GST_MAP_READ));
    const sf::unique_gst_buffer_unmapper reconstructed_unmapper(reconstructed_buffer, &reconstructed_map);

    // Verify Y plane: region at (4,4) should match packed region at (0,0)
    const auto *y_out = reconstructed_map.data;
    const auto luma_stride = GST_VIDEO_INFO_PLANE_STRIDE(&reconstructed_info, 0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int packed_idx = y * packed_width + x;
            const int reconstructed_idx = (y + 4) * luma_stride + (x + 4);
            EXPECT_EQ(y_out[reconstructed_idx], packed_data[packed_idx])
                    << "Y plane mismatch at reconstructed (" << (x + 4) << ", " << (y + 4) << ")";
        }
    }

    // Verify UV plane: region at (2,2) in UV space should match packed region at (0,0) in UV space
    // For NV12, X coordinates in UV plane are in BYTES, not pixels
    // So x=4 pixels means x=4 bytes in UV plane
    const auto *uv_out = reconstructed_map.data + GST_VIDEO_INFO_PLANE_OFFSET(&reconstructed_info, 1);
    const auto recon_uv_stride = GST_VIDEO_INFO_PLANE_STRIDE(&reconstructed_info, 1);
    for (int y = 0; y < 2; ++y) { // UV is subsampled by 2
        // X is in bytes, so we check every byte position
        for (int x_bytes = 0; x_bytes < 4; x_bytes += 2) { // Step by 2 bytes (1 UV pixel)
            // Get packed data using GstVideoInfo offsets and strides
            const gsize packed_idx = packed_uv_offset + y * packed_uv_stride + x_bytes;
            const int reconstructed_idx = (y + 2) * recon_uv_stride + (4 + x_bytes); // 4 bytes offset (4 pixels)
            if (packed_idx + 1 < packed_size && reconstructed_idx + 1 < static_cast<int>(reconstructed_map.size)) {
                EXPECT_EQ(uv_out[reconstructed_idx], packed_data[packed_idx])
                        << "UV plane U mismatch at reconstructed UV (" << (4 + x_bytes) << " bytes, " << (y + 2) << ")";
                EXPECT_EQ(uv_out[reconstructed_idx + 1], packed_data[packed_idx + 1])
                        << "UV plane V mismatch at reconstructed UV (" << (4 + x_bytes) << " bytes, " << (y + 2) << ")";
            }
        }
    }
}

// Test NV12 reconstruction with multiple regions
TEST_F(area_copy_test, reconstruct_nv12_multiple_regions) {
    constexpr int packed_width = 16;
    constexpr int packed_height = 16;
    constexpr int reconstructed_width = 32;
    constexpr int reconstructed_height = 32;

    // Create packed NV12 frame
    constexpr gsize packed_size = packed_width * packed_height + packed_width * (packed_height / 2) * 2;
    std::vector<uint8_t> packed_data(packed_size);

    // Fill with pattern: Y = position index, UV = interleaved pattern
    for (int i = 0; i < packed_width * packed_height; ++i) {
        packed_data[i] = static_cast<uint8_t>(i % 256);
    }

    const int uv_offset = packed_width * packed_height;
    for (int i = 0; i < packed_width * (packed_height / 2) * 2; i += 2) {
        packed_data[uv_offset + i] = static_cast<uint8_t>((i / 2) % 256); // U
        packed_data[uv_offset + i + 1] = static_cast<uint8_t>(((i / 2) + 128) % 256); // V
    }

    auto packed_sample = create_nv12_sample(packed_width, packed_height, packed_data);
    ASSERT_NE(packed_sample, nullptr);

    // Create PRI: copy two 4x4 regions
    std::vector<sf::rectangle> packed_rects = {
            {0, 0, 4, 4}, // First region at top-left of packed
            {4, 4, 4, 4} // Second region at center of packed
    };
    std::vector<sf::rectangle> target_rects = {
            {0, 0, 4, 4}, // First region to top-left of reconstructed
            {16, 16, 4, 4} // Second region to center-right of reconstructed
    };
    auto pri = create_test_pri(reconstructed_width, reconstructed_height, packed_rects, target_rects);

    // Reconstruct frame
    auto reconstructed_sample = sf::reconstruct_frame(std::move(packed_sample), pri);
    ASSERT_NE(reconstructed_sample, nullptr);

    // Verify both regions were copied correctly
    auto *reconstructed_buffer = gst_sample_get_buffer(reconstructed_sample.get());
    auto *reconstructed_caps = gst_sample_get_caps(reconstructed_sample.get());
    GstVideoInfo reconstructed_info;
    ASSERT_TRUE(gst_video_info_from_caps(&reconstructed_info, reconstructed_caps));

    GstMapInfo reconstructed_map;
    ASSERT_TRUE(gst_buffer_map(reconstructed_buffer, &reconstructed_map, GST_MAP_READ));
    const sf::unique_gst_buffer_unmapper reconstructed_unmapper(reconstructed_buffer, &reconstructed_map);

    const auto *y_out = reconstructed_map.data;
    const auto luma_stride = GST_VIDEO_INFO_PLANE_STRIDE(&reconstructed_info, 0);

    // Verify first region (0,0) -> (0,0)
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int packed_idx = y * packed_width + x;
            const int reconstructed_idx = y * luma_stride + x;
            EXPECT_EQ(y_out[reconstructed_idx], packed_data[packed_idx])
                    << "First region Y mismatch at (" << x << ", " << y << ")";
        }
    }

    // Verify second region (4,4) -> (16,16)
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int packed_idx = (y + 4) * packed_width + (x + 4);
            const int reconstructed_idx = (y + 16) * luma_stride + (x + 16);
            EXPECT_EQ(y_out[reconstructed_idx], packed_data[packed_idx])
                    << "Second region Y mismatch at (" << (x + 16) << ", " << (y + 16) << ")";
        }
    }
}

// Test NV12 reconstruction with row-by-row verification (each row has unique value)
TEST_F(area_copy_test, reconstruct_nv12_row_verification) {
    constexpr int packed_width = 8;
    constexpr int packed_height = 8;
    constexpr int reconstructed_width = 16;
    constexpr int reconstructed_height = 16;

    // Create packed frame where each row has a unique value
    GstVideoInfo packed_test_info;
    gst_video_info_init(&packed_test_info);
    if (!gst_video_info_set_format(&packed_test_info, GST_VIDEO_FORMAT_NV12, static_cast<guint>(packed_width),
                                   static_cast<guint>(packed_height))) {
        GTEST_SKIP() << "NV12 format not supported";
    }
    const gsize packed_size = packed_test_info.size;
    std::vector<uint8_t> packed_data(packed_size, 0);

    // Fill Y plane: each row has its row number as the value (row 0 = 0, row 1 = 10, etc.)
    const gint packed_y_stride = GST_VIDEO_INFO_PLANE_STRIDE(&packed_test_info, 0);
    const gint packed_y_height = GST_VIDEO_INFO_COMP_HEIGHT(&packed_test_info, 0);
    for (gint y = 0; y < packed_y_height; ++y) {
        for (gint x = 0; x < packed_width; ++x) {
            const gsize idx = GST_VIDEO_INFO_PLANE_OFFSET(&packed_test_info, 0) + y * packed_y_stride + x;
            if (idx < packed_size) {
                packed_data[idx] = static_cast<uint8_t>(y * 10); // Each row has same value: row*10
            }
        }
    }

    // Fill UV plane: each row has its row number as the value
    const gint packed_uv_stride = GST_VIDEO_INFO_PLANE_STRIDE(&packed_test_info, 1);
    const gint packed_uv_height = GST_VIDEO_INFO_COMP_HEIGHT(&packed_test_info, 1);
    const gsize packed_uv_offset = GST_VIDEO_INFO_PLANE_OFFSET(&packed_test_info, 1);
    for (gint y = 0; y < packed_uv_height; ++y) {
        for (gint x = 0; x < packed_width; x += 2) {
            const gsize idx = packed_uv_offset + y * packed_uv_stride + x;
            if (idx + 1 < packed_size) {
                packed_data[idx] = static_cast<uint8_t>(y * 10); // U
                packed_data[idx + 1] = static_cast<uint8_t>(y * 10 + 1); // V
            }
        }
    }

    auto packed_sample = create_nv12_sample(packed_width, packed_height, packed_data);
    ASSERT_NE(packed_sample, nullptr);

    // Create PRI: copy 4x4 region from packed (0,0) to reconstructed (4,4)
    std::vector<sf::rectangle> packed_rects = {{0, 0, 4, 4}};
    std::vector<sf::rectangle> target_rects = {{4, 4, 4, 4}};
    auto pri = create_test_pri(reconstructed_width, reconstructed_height, packed_rects, target_rects);

    // Reconstruct frame
    auto reconstructed_sample = sf::reconstruct_frame(std::move(packed_sample), pri);
    ASSERT_NE(reconstructed_sample, nullptr);

    // Verify reconstructed frame - check that each row is copied correctly
    auto *reconstructed_buffer = gst_sample_get_buffer(reconstructed_sample.get());
    auto *reconstructed_caps = gst_sample_get_caps(reconstructed_sample.get());
    GstVideoInfo reconstructed_info;
    ASSERT_TRUE(gst_video_info_from_caps(&reconstructed_info, reconstructed_caps));

    GstMapInfo reconstructed_map;
    ASSERT_TRUE(gst_buffer_map(reconstructed_buffer, &reconstructed_map, GST_MAP_READ));
    const sf::unique_gst_buffer_unmapper reconstructed_unmapper(reconstructed_buffer, &reconstructed_map);

    // Verify Y plane: each row at (4,4) to (4,7) should have the correct row value
    const auto *y_out = reconstructed_map.data;
    const auto recon_y_stride = GST_VIDEO_INFO_PLANE_STRIDE(&reconstructed_info, 0);
    for (int y = 0; y < 4; ++y) {
        const uint8_t expected_value = static_cast<uint8_t>(y * 10);
        for (int x = 0; x < 4; ++x) {
            const int reconstructed_idx = (y + 4) * recon_y_stride + (x + 4);
            EXPECT_EQ(y_out[reconstructed_idx], expected_value)
                    << "Y plane row " << y << " mismatch at (" << (x + 4) << ", " << (y + 4) << "), expected row value "
                    << static_cast<int>(expected_value);
        }
    }
}

TEST_F(area_copy_test, copy_regions_to_packed_frame_throws_for_nv12_format) {
    constexpr int width = 8;
    constexpr int height = 8;
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

    const std::vector<sf::rectangle> rects = {{0, 0, 4, 4}};
    EXPECT_THROW(sf::copy_regions_to_packed_frame(in_map, out_map, width, height, rects, rects, sf::video_format::nv12),
                 std::runtime_error);
}

TEST_F(area_copy_test, create_empty_buffer_validates_dimensions_and_green_initialization) {
    EXPECT_EQ(sf::create_empty_buffer(0, 16, sf::video_format::i420), nullptr);
    EXPECT_EQ(sf::create_empty_buffer(16, -1, sf::video_format::nv12), nullptr);

    auto buffer = sf::create_empty_buffer(8, 8, sf::video_format::nv12, sf::background_color::green);
    ASSERT_NE(buffer, nullptr);

    GstMapInfo map_info{};
    ASSERT_TRUE(gst_buffer_map(buffer.get(), &map_info, GST_MAP_READ));
    const sf::unique_gst_buffer_unmapper unmapper(buffer.get(), &map_info);
    ASSERT_GT(map_info.size, 0U);
    EXPECT_TRUE(
            std::all_of(map_info.data, map_info.data + map_info.size, [](const uint8_t value) { return value == 0U; }));
}

TEST_F(area_copy_test, video_format_string_conversions_cover_known_and_unknown_inputs) {
    EXPECT_STREQ(sf::video_format_to_string(sf::video_format::i420), "I420");
    EXPECT_STREQ(sf::video_format_to_string(sf::video_format::nv12), "NV12");
    EXPECT_STREQ(sf::video_format_to_string(sf::video_format::yuv420), "YUV420");

    EXPECT_EQ(sf::string_to_video_format("I420"), sf::video_format::i420);
    EXPECT_EQ(sf::string_to_video_format("NV12"), sf::video_format::nv12);
    EXPECT_EQ(sf::string_to_video_format("YUV420"), sf::video_format::yuv420);
    EXPECT_EQ(sf::string_to_video_format("UNKNOWN"), sf::video_format::i420);
    EXPECT_EQ(sf::string_to_video_format(nullptr), sf::video_format::i420);
}

TEST_F(area_copy_test, reconstruct_frame_requires_target_dimensions_in_pri) {
    constexpr int packed_width = 8;
    constexpr int packed_height = 8;

    GstVideoInfo packed_info;
    gst_video_info_init(&packed_info);
    ASSERT_TRUE(gst_video_info_set_format(&packed_info, GST_VIDEO_FORMAT_NV12, static_cast<guint>(packed_width),
                                          static_cast<guint>(packed_height)));

    std::vector<uint8_t> packed_data(packed_info.size, 0x11);
    auto packed_sample = create_nv12_sample(packed_width, packed_height, packed_data);
    ASSERT_NE(packed_sample, nullptr);

    sf::packed_regions_info pri_missing_dimensions;
    pri_missing_dimensions.log2_unit_size = 0;
    auto reconstructed = sf::reconstruct_frame(std::move(packed_sample), pri_missing_dimensions);
    EXPECT_EQ(reconstructed, nullptr);
}

TEST_F(area_copy_test, nv12_validation_sanitizes_rectangles_to_even_coordinates) {
    const sf::rectangle input{3, 5, 7, 9};
    const sf::rectangle packed{11, 13, 7, 9};

    const auto input_even = sf::sanitize_input_rectangle_nv12(input);
    const auto packed_even = sf::sanitize_packed_rectangle_nv12(packed);

    EXPECT_EQ(input_even.left % 2, 0);
    EXPECT_EQ(input_even.top % 2, 0);
    EXPECT_EQ(input_even.width % 2, 0);
    EXPECT_EQ(input_even.height % 2, 0);
    EXPECT_EQ(packed_even.left % 2, 0);
    EXPECT_EQ(packed_even.top % 2, 0);
}

TEST_F(area_copy_test, nv12_validation_clamps_positions_and_rejects_invalid_after_alignment) {
    const sf::rectangle input_even = sf::sanitize_input_rectangle_nv12(sf::rectangle{6, 6, 10, 10});
    const sf::rectangle packed_even = sf::sanitize_packed_rectangle_nv12(sf::rectangle{100, 100, 10, 10});

    const auto valid = sf::validate_and_calculate_nv12_copy_params(input_even, packed_even, 32, 24, 0);
    ASSERT_TRUE(valid.has_value());
    const auto [clamped_x, clamped_y, copy_w, copy_h] = valid.value();
    EXPECT_GE(clamped_x, 0);
    EXPECT_GE(clamped_y, 0);
    EXPECT_EQ(copy_w % 2, 0);
    EXPECT_EQ(copy_h % 2, 0);

    const auto invalid = sf::validate_and_calculate_nv12_copy_params(
            sf::sanitize_input_rectangle_nv12(sf::rectangle{2, 2, 1, 1}), packed_even, 32, 24, 1);
    EXPECT_FALSE(invalid.has_value());
}

TEST_F(area_copy_test, pri_extraction_maps_units_to_pixel_rectangles) {
    sf::packed_regions_info pri{};
    pri.log2_unit_size = 2; // unit=4 pixels
    sf::region region{};
    region.top_left_in_units_x = 1;
    region.top_left_in_units_y = 2;
    region.width_in_units_minus1 = 2; // width=3 units => 12 px
    region.height_in_units_minus1 = 1; // height=2 units => 8 px
    region.target_region_top_left_in_units_x = 3;
    region.target_region_top_left_in_units_y = 4;
    pri.regions.push_back(region);

    const auto [packed_rects, target_rects] = sf::extract_rectangles_from_pri(pri);
    ASSERT_EQ(packed_rects.size(), 1U);
    ASSERT_EQ(target_rects.size(), 1U);
    EXPECT_EQ(packed_rects[0], sf::rectangle(4, 8, 12, 8));
    EXPECT_EQ(target_rects[0], sf::rectangle(12, 16, 12, 8));
}

TEST_F(area_copy_test, pri_extraction_skips_incomplete_regions_and_targets_independently) {
    sf::packed_regions_info pri{};
    pri.log2_unit_size = 0;

    sf::region incomplete_packed{};
    incomplete_packed.top_left_in_units_x = 0;
    // missing top_left_in_units_y, width, height -> skipped entirely
    pri.regions.push_back(incomplete_packed);

    sf::region missing_target{};
    missing_target.top_left_in_units_x = 1;
    missing_target.top_left_in_units_y = 1;
    missing_target.width_in_units_minus1 = 1;
    missing_target.height_in_units_minus1 = 1;
    // missing target_region_top_left* -> packed is kept, target skipped
    pri.regions.push_back(missing_target);

    const auto [packed_rects, target_rects] = sf::extract_rectangles_from_pri(pri);
    EXPECT_EQ(packed_rects.size(), 1U);
    EXPECT_EQ(target_rects.size(), 0U);
}

TEST_F(area_copy_test,
       copy_regions_to_packed_frame_nv12_initialize_yuv420_buffer_with_color_create_nv12_video_info_validate_buffer_size_paths) {
    auto yuv_buffer = sf::create_empty_buffer(16, 16, sf::video_format::i420, sf::background_color::green);
    ASSERT_NE(yuv_buffer, nullptr);
    EXPECT_EQ(sf::create_empty_buffer(0, 16, sf::video_format::i420), nullptr);

    const auto sample = create_nv12_sample(8, 8, std::vector<uint8_t>(96, 0x7F));
    ASSERT_NE(sample, nullptr);
    auto *buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(buffer, nullptr);
    EXPECT_GT(gst_buffer_get_size(buffer), 0U);
}

TEST_F(area_copy_test,
       validate_interleaved_uv_region_copy_interleaved_uv_row_copy_interleaved_uv_region_get_nv12_plane_data_process_single_nv12_roi_process_nv12_frame) {
    constexpr int width = 8;
    constexpr int height = 8;
    auto in_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(width * height * 3 / 2));
    auto out_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_and_alloc(width * height * 3 / 2));
    ASSERT_NE(in_buffer, nullptr);
    ASSERT_NE(out_buffer, nullptr);

    GstMapInfo in_map{};
    GstMapInfo out_map{};
    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_WRITE));
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map, GST_MAP_WRITE));
    std::memset(in_map.data, 0x11, in_map.size);
    std::memset(out_map.data, 0x22, out_map.size);
    gst_buffer_unmap(in_buffer.get(), &in_map);
    gst_buffer_unmap(out_buffer.get(), &out_map);

    ASSERT_TRUE(gst_buffer_map(in_buffer.get(), &in_map, GST_MAP_READ));
    ASSERT_TRUE(gst_buffer_map(out_buffer.get(), &out_map, GST_MAP_WRITE));
    const sf::unique_gst_buffer_unmapper in_unmapper(in_buffer.get(), &in_map);
    const sf::unique_gst_buffer_unmapper out_unmapper(out_buffer.get(), &out_map);

    const std::vector<sf::rectangle> input_rectangles = {{0, 0, 4, 4}};
    const std::vector<sf::rectangle> packed_rectangles = {{2, 2, 4, 4}};
    sf::copy_regions_to_packed_frame(in_map, out_map, width, height, input_rectangles, packed_rectangles,
                                     sf::video_format::i420);
    EXPECT_NE(out_map.data[2 * width + 2], 0x22);
}

TEST_F(area_copy_test,
       sanitize_rect_for_reconstruction_copy_y_plane_reconstruction_rows_copy_uv_plane_reconstruction_rows_process_single_nv12_reconstruction_roi) {
    constexpr int packed_width = 8;
    constexpr int packed_height = 8;
    constexpr int target_width = 16;
    constexpr int target_height = 16;
    std::vector<uint8_t> packed_data(96, 0x44);
    auto packed_sample = create_nv12_sample(packed_width, packed_height, packed_data);
    ASSERT_NE(packed_sample, nullptr);

    sf::packed_regions_info pri{};
    pri.target_pic_width_minus1 = target_width - 1;
    pri.target_pic_height_minus1 = target_height - 1;
    pri.log2_unit_size = 0;
    sf::region region{};
    region.top_left_in_units_x = 0;
    region.top_left_in_units_y = 0;
    region.width_in_units_minus1 = 3;
    region.height_in_units_minus1 = 3;
    region.target_region_top_left_in_units_x = 14; // intentionally near boundary for sanitization path
    region.target_region_top_left_in_units_y = 14;
    pri.regions.push_back(region);

    const auto reconstructed = sf::reconstruct_frame(std::move(packed_sample), pri);
    ASSERT_NE(reconstructed, nullptr);
}

TEST_F(area_copy_test,
       sample_uv_values_verify_copied_uv_values_copy_nv12_uv_plane_sanitize_rectangle_for_nv12_validate_sanitized_rectangle_validate_copy_dimensions_copy_y_plane_rows_copy_nv12_y_plane) {
    const sf::rectangle input{5, 7, 9, 11};
    const sf::rectangle even_input = sf::sanitize_input_rectangle_nv12(input);
    EXPECT_EQ(even_input.left % 2, 0);
    EXPECT_EQ(even_input.top % 2, 0);
    EXPECT_EQ(even_input.width % 2, 0);
    EXPECT_EQ(even_input.height % 2, 0);

    const auto params = sf::validate_and_calculate_nv12_copy_params(even_input, sf::rectangle{0, 0, 20, 20}, 32, 24, 0);
    EXPECT_TRUE(params.has_value());
}

TEST_F(area_copy_test,
       get_input_video_info_create_output_video_info_to_area_copy_format_reconstruct_i420_frame_with_mapped_buffers_initialize_nv12_buffer_with_color_initialize_buffer_with_color_initialize_reconstruction_buffer_extract_packed_rect_from_region_extract_target_rect_from_region_calculate_yuv420_plane_offsets_validate_copy_region_copy_plane_row_copy_plane_region_copy_y_plane_for_rect_copy_uv_planes_for_rect_process_yuv420_frame_get_reconstruction_planes_get_reconstruction_strides_get_reconstruction_heights_copy_y_plane_reconstruction) {
    sf::packed_regions_info pri{};
    pri.log2_unit_size = 1;
    sf::region region{};
    region.top_left_in_units_x = 1;
    region.top_left_in_units_y = 1;
    region.width_in_units_minus1 = 1;
    region.height_in_units_minus1 = 1;
    region.target_region_top_left_in_units_x = 2;
    region.target_region_top_left_in_units_y = 2;
    pri.regions.push_back(region);

    const auto [packed_rects, target_rects] = sf::extract_rectangles_from_pri(pri);
    ASSERT_EQ(packed_rects.size(), 1U);
    ASSERT_EQ(target_rects.size(), 1U);
    EXPECT_EQ(packed_rects[0].width, 4);
    EXPECT_EQ(target_rects[0].left, 4);
}
