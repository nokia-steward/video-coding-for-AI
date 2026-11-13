// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <algorithm>
#include <cstdint>
#include <vector>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>

#include "../common/gst_test_fixture.h"
#include "background_blur.h"
#include "gst_raii.h"

namespace {
    struct test_video_buffer {
        GstVideoInfo video_info;
        sf::unique_gst_buffer_ptr buffer;
        bool is_valid;
    };

    [[nodiscard]] test_video_buffer create_test_video_buffer(const GstVideoFormat format, const int width,
                                                             const int height) {
        test_video_buffer result{};
        gst_video_info_init(&result.video_info);
        if (!gst_video_info_set_format(&result.video_info, format, static_cast<guint>(width),
                                       static_cast<guint>(height))) {
            result.is_valid = false;
            return result;
        }

        result.buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, result.video_info.size, nullptr));
        result.is_valid = static_cast<bool>(result.buffer);
        return result;
    }

    [[nodiscard]] std::vector<uint8_t> snapshot_buffer_bytes(const sf::unique_gst_buffer_ptr &buffer) {
        GstMapInfo map_info{};
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_READ)) {
            return {};
        }
        const sf::unique_gst_buffer_unmapper unmapper(buffer.get(), &map_info);
        return {map_info.data, map_info.data + map_info.size};
    }

    bool fill_buffer_with_test_pattern(const sf::unique_gst_buffer_ptr &buffer, const GstVideoInfo *video_info) {
        GstVideoFrame frame{};
        if (!gst_video_frame_map(&frame, video_info, buffer.get(), GST_MAP_WRITE)) {
            return false;
        }

        const GstVideoFormat format = GST_VIDEO_INFO_FORMAT(video_info);
        if (format == GST_VIDEO_FORMAT_NV12) {
            auto *y_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
            auto *uv_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 1));
            const int luma_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            const int uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1);
            const int luma_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 0);
            const int luma_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 0);
            const int uv_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 1);
            const int uv_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 1);

            for (int y = 0; y < luma_height; ++y) {
                for (int x = 0; x < luma_width; ++x) {
                    y_plane[y * luma_stride + x] = ((x + y) % 2 == 0) ? 32 : 220;
                }
            }

            for (int y = 0; y < uv_height; ++y) {
                for (int x = 0; x < uv_width; ++x) {
                    uv_plane[y * uv_stride + x * 2] = ((x + y) % 3 == 0) ? 96 : 160;
                    uv_plane[y * uv_stride + x * 2 + 1] = ((x + y) % 4 == 0) ? 80 : 200;
                }
            }
        } else if (format == GST_VIDEO_FORMAT_I420) {
            auto *y_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
            auto *u_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 1));
            auto *v_plane = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 2));
            const int luma_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            const int chroma_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 1);
            const int chroma_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 2);
            const int luma_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 0);
            const int luma_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 0);
            const int chroma_width = GST_VIDEO_INFO_COMP_WIDTH(video_info, 1);
            const int chroma_height = GST_VIDEO_INFO_COMP_HEIGHT(video_info, 1);

            for (int y = 0; y < luma_height; ++y) {
                for (int x = 0; x < luma_width; ++x) {
                    y_plane[y * luma_stride + x] = ((x + y) % 2 == 0) ? 24 : 232;
                }
            }
            for (int y = 0; y < chroma_height; ++y) {
                for (int x = 0; x < chroma_width; ++x) {
                    u_plane[y * chroma_u_stride + x] = ((x + y) % 3 == 0) ? 90 : 170;
                    v_plane[y * chroma_v_stride + x] = ((x + y) % 4 == 0) ? 72 : 210;
                }
            }
        } else {
            gst_video_frame_unmap(&frame);
            return false;
        }

        gst_video_frame_unmap(&frame);
        return true;
    }

    [[nodiscard]] bool luma_plane_roi_pixels_match(const sf::unique_gst_buffer_ptr &before_buffer,
                                                   const sf::unique_gst_buffer_ptr &after_buffer,
                                                   const GstVideoInfo *video_info, const sf::rectangle &roi) {
        GstVideoFrame before_frame{};
        GstVideoFrame after_frame{};
        if (!gst_video_frame_map(&before_frame, video_info, before_buffer.get(), GST_MAP_READ)) {
            return false;
        }
        if (!gst_video_frame_map(&after_frame, video_info, after_buffer.get(), GST_MAP_READ)) {
            gst_video_frame_unmap(&before_frame);
            return false;
        }

        const auto *before_y = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&before_frame, 0));
        const auto *after_y = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&after_frame, 0));
        const int before_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&before_frame, 0);
        const int after_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&after_frame, 0);
        const int width = GST_VIDEO_FRAME_COMP_WIDTH(&before_frame, 0);
        const int height = GST_VIDEO_FRAME_COMP_HEIGHT(&before_frame, 0);

        const int start_x = std::clamp(roi.left, 0, width);
        const int start_y = std::clamp(roi.top, 0, height);
        const int end_x = std::clamp(roi.left + roi.width, 0, width);
        const int end_y = std::clamp(roi.top + roi.height, 0, height);

        bool matches = true;
        for (int y = start_y; y < end_y && matches; ++y) {
            for (int x = start_x; x < end_x; ++x) {
                if (before_y[y * before_stride + x] != after_y[y * after_stride + x]) {
                    matches = false;
                    break;
                }
            }
        }

        gst_video_frame_unmap(&after_frame);
        gst_video_frame_unmap(&before_frame);
        return matches;
    }

    [[nodiscard]] bool any_y_background_pixel_changed(const sf::unique_gst_buffer_ptr &before_buffer,
                                                      const sf::unique_gst_buffer_ptr &after_buffer,
                                                      const GstVideoInfo *video_info, const sf::rectangle &roi) {
        GstVideoFrame before_frame{};
        GstVideoFrame after_frame{};
        if (!gst_video_frame_map(&before_frame, video_info, before_buffer.get(), GST_MAP_READ)) {
            return false;
        }
        if (!gst_video_frame_map(&after_frame, video_info, after_buffer.get(), GST_MAP_READ)) {
            gst_video_frame_unmap(&before_frame);
            return false;
        }

        const auto *before_y = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&before_frame, 0));
        const auto *after_y = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&after_frame, 0));
        const int before_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&before_frame, 0);
        const int after_stride = GST_VIDEO_FRAME_PLANE_STRIDE(&after_frame, 0);
        const int width = GST_VIDEO_FRAME_COMP_WIDTH(&before_frame, 0);
        const int height = GST_VIDEO_FRAME_COMP_HEIGHT(&before_frame, 0);

        bool changed = false;
        for (int y = 0; y < height && !changed; ++y) {
            for (int x = 0; x < width; ++x) {
                if (roi.contains(x, y)) {
                    continue;
                }
                if (before_y[y * before_stride + x] != after_y[y * after_stride + x]) {
                    changed = true;
                    break;
                }
            }
        }

        gst_video_frame_unmap(&after_frame);
        gst_video_frame_unmap(&before_frame);
        return changed;
    }

    [[nodiscard]] bool apply_blur_to_frame(const test_video_buffer &frame, const std::vector<sf::rectangle> &rois,
                                           const double sigma, const sf::video_format format,
                                           const int kernel_size = 0) {
        sf::unique_gst_buffer_no_delete_ptr no_delete_buffer(frame.buffer.get());
        const int width = GST_VIDEO_INFO_WIDTH(&frame.video_info);
        const int height = GST_VIDEO_INFO_HEIGHT(&frame.video_info);
        return sf::apply_background_blur(no_delete_buffer, rois, sigma, width, height, format, kernel_size);
    }
} // namespace

struct background_blur_test : GStreamerTestFixture {};

TEST_F(background_blur_test, rejects_invalid_sigma_and_even_kernel_size) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    EXPECT_FALSE(apply_blur_to_frame(frame, {}, 0.0, sf::video_format::nv12));
    EXPECT_FALSE(apply_blur_to_frame(frame, {}, 1.2, sf::video_format::nv12, 4));
}

TEST_F(background_blur_test, blurs_nv12_frame_when_background_exists) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    const auto before = snapshot_buffer_bytes(frame.buffer);
    ASSERT_FALSE(before.empty());

    ASSERT_TRUE(apply_blur_to_frame(frame, {}, 1.5, sf::video_format::nv12));
    const auto after = snapshot_buffer_bytes(frame.buffer);
    ASSERT_FALSE(after.empty());

    EXPECT_NE(before, after);
}

TEST_F(background_blur_test, blurs_i420_frame_when_background_exists) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_I420, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    const auto before = snapshot_buffer_bytes(frame.buffer);
    ASSERT_FALSE(before.empty());

    ASSERT_TRUE(apply_blur_to_frame(frame, {}, 1.5, sf::video_format::i420));
    const auto after = snapshot_buffer_bytes(frame.buffer);
    ASSERT_FALSE(after.empty());

    EXPECT_NE(before, after);
}

TEST_F(background_blur_test, keeps_nv12_frame_unchanged_when_full_frame_is_roi) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    const auto before = snapshot_buffer_bytes(frame.buffer);
    ASSERT_FALSE(before.empty());

    std::vector<sf::rectangle> rois{sf::rectangle{0, 0, 32, 24}};
    ASSERT_TRUE(apply_blur_to_frame(frame, rois, 1.5, sf::video_format::nv12));
    const auto after = snapshot_buffer_bytes(frame.buffer);
    ASSERT_FALSE(after.empty());

    EXPECT_EQ(before, after);
}

TEST_F(background_blur_test, preserves_roi_pixels_while_blurring_nv12_background) {
    auto baseline = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    auto blurred = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(baseline.is_valid);
    ASSERT_TRUE(blurred.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(baseline.buffer, &baseline.video_info));
    ASSERT_TRUE(fill_buffer_with_test_pattern(blurred.buffer, &blurred.video_info));

    const sf::rectangle roi{8, 6, 10, 8};
    std::vector<sf::rectangle> rois{roi};

    ASSERT_TRUE(apply_blur_to_frame(blurred, rois, 1.5, sf::video_format::nv12));
    EXPECT_TRUE(luma_plane_roi_pixels_match(baseline.buffer, blurred.buffer, &blurred.video_info, roi));
    EXPECT_TRUE(any_y_background_pixel_changed(baseline.buffer, blurred.buffer, &blurred.video_info, roi));
}

TEST_F(background_blur_test, rejects_invalid_frame_dimensions) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    sf::unique_gst_buffer_no_delete_ptr no_delete_buffer(frame.buffer.get());
    EXPECT_FALSE(sf::apply_background_blur(no_delete_buffer, {}, 1.2, 0, 24, sf::video_format::nv12));
    EXPECT_FALSE(sf::apply_background_blur(no_delete_buffer, {}, 1.2, 32, -1, sf::video_format::nv12));
}

TEST_F(background_blur_test, rejects_unsupported_format_identifier) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    sf::unique_gst_buffer_no_delete_ptr no_delete_buffer(frame.buffer.get());
    EXPECT_FALSE(sf::apply_background_blur(no_delete_buffer, {}, 1.2, 32, 24, sf::video_format::yuv420));
}

TEST_F(background_blur_test, out_of_bounds_roi_is_clamped_without_failure) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    std::vector<sf::rectangle> rois = {
            sf::rectangle{-20, -10, 18, 14},
            sf::rectangle{24, 18, 20, 20},
    };

    EXPECT_TRUE(apply_blur_to_frame(frame, rois, 1.4, sf::video_format::nv12));
}

TEST_F(background_blur_test,
       extract_uv_planes_write_back_uv_planes_blur_single_chroma_plane_and_blur_nv12_frame_paths_modify_buffer) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    const auto before = snapshot_buffer_bytes(frame.buffer);
    ASSERT_TRUE(apply_blur_to_frame(frame, {}, 1.6, sf::video_format::nv12));
    const auto after = snapshot_buffer_bytes(frame.buffer);
    EXPECT_NE(before, after);
}

TEST_F(background_blur_test,
       apply_horizontal_blur_row_apply_vertical_blur_column_blur_plane_and_blur_yuv420_frame_paths_modify_buffer) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_I420, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    const auto before = snapshot_buffer_bytes(frame.buffer);
    ASSERT_TRUE(apply_blur_to_frame(frame, {}, 1.6, sf::video_format::i420));
    const auto after = snapshot_buffer_bytes(frame.buffer);
    EXPECT_NE(before, after);
}

TEST_F(background_blur_test, calculate_kernel_size_and_generate_gaussian_kernel_parameter_guards) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 32, 24);
    ASSERT_TRUE(frame.is_valid);
    ASSERT_TRUE(fill_buffer_with_test_pattern(frame.buffer, &frame.video_info));

    EXPECT_FALSE(apply_blur_to_frame(frame, {}, 0.0, sf::video_format::nv12));
    EXPECT_FALSE(apply_blur_to_frame(frame, {}, 1.2, sf::video_format::nv12, 4));
    EXPECT_TRUE(apply_blur_to_frame(frame, {}, 1.2, sf::video_format::nv12, 5));
}
