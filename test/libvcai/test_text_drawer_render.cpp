// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <algorithm>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>

#include "../common/gst_test_fixture.h"
#include "gst_raii.h"
#include "text_drawer.h"

namespace {
    struct test_video_buffer {
        GstVideoInfo video_info;
        sf::unique_gst_buffer_ptr buffer;
        bool is_valid;
    };

    /**
     * @brief Creates a video buffer initialized to a constant byte value.
     * @param format Video format.
     * @param width Frame width.
     * @param height Frame height.
     * @param fill_value Byte value used to initialize the full buffer memory.
     * @return Allocated test buffer state.
     */
    [[nodiscard]] test_video_buffer create_test_video_buffer(const GstVideoFormat format, const int width,
                                                             const int height, const uint8_t fill_value) {
        test_video_buffer result{};
        gst_video_info_init(&result.video_info);
        if (!gst_video_info_set_format(&result.video_info, format, static_cast<guint>(width),
                                       static_cast<guint>(height))) {
            result.is_valid = false;
            return result;
        }

        result.buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, result.video_info.size, nullptr));
        if (!result.buffer) {
            result.is_valid = false;
            return result;
        }

        gst_buffer_memset(result.buffer.get(), 0, fill_value, result.video_info.size);
        result.is_valid = true;
        return result;
    }

    /**
     * @brief Checks whether a mapped buffer contains any byte different from expected value.
     * @param buffer Buffer to inspect.
     * @param expected_value Expected byte value.
     * @return true if at least one byte differs from expected value.
     */
    [[nodiscard]] bool has_any_byte_different_from(const sf::unique_gst_buffer_ptr &buffer,
                                                   const uint8_t expected_value) {
        GstMapInfo map_info{};
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_READ)) {
            return false;
        }
        const sf::unique_gst_buffer_unmapper unmapper(buffer.get(), &map_info);
        return std::any_of(map_info.data, map_info.data + map_info.size,
                           [expected_value](const uint8_t value) { return value != expected_value; });
    }

    /**
     * @brief Checks whether a mapped buffer contains only the expected byte value.
     * @param buffer Buffer to inspect.
     * @param expected_value Expected byte value.
     * @return true when every byte equals expected_value.
     */
    [[nodiscard]] bool all_bytes_equal_to(const sf::unique_gst_buffer_ptr &buffer, const uint8_t expected_value) {
        GstMapInfo map_info{};
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_READ)) {
            return false;
        }
        const sf::unique_gst_buffer_unmapper unmapper(buffer.get(), &map_info);
        return std::all_of(map_info.data, map_info.data + map_info.size,
                           [expected_value](const uint8_t value) { return value == expected_value; });
    }
} // namespace

struct text_drawer_render_test : GStreamerTestFixture {};

TEST_F(text_drawer_render_test, draw_text_on_nv12_changes_buffer_content) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 64, 32, 0);
    ASSERT_TRUE(frame.is_valid);

    ASSERT_TRUE(sf::draw_text_on_nv12_frame(frame.buffer.get(), &frame.video_info, "12:34", 4, 6, sf::yuv_color::red));
    EXPECT_TRUE(has_any_byte_different_from(frame.buffer, 0));
}

TEST_F(text_drawer_render_test, draw_text_on_i420_changes_buffer_content) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_I420, 64, 32, 0);
    ASSERT_TRUE(frame.is_valid);

    ASSERT_TRUE(
            sf::draw_text_on_i420_frame(frame.buffer.get(), &frame.video_info, "56.78", 5, 5, sf::yuv_color::white));
    EXPECT_TRUE(has_any_byte_different_from(frame.buffer, 0));
}

TEST_F(text_drawer_render_test, draw_large_red_text_on_nv12_changes_buffer_content) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 96, 48, 0);
    ASSERT_TRUE(frame.is_valid);

    ASSERT_TRUE(sf::draw_text_on_nv12_frame_large_red(frame.buffer.get(), &frame.video_info, "12:34", 4, 6,
                                                      sf::yuv_color::red));
    EXPECT_TRUE(has_any_byte_different_from(frame.buffer, 0));
}

TEST_F(text_drawer_render_test, draw_large_red_text_on_i420_changes_buffer_content) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_I420, 96, 48, 0);
    ASSERT_TRUE(frame.is_valid);

    ASSERT_TRUE(sf::draw_text_on_i420_frame_large_red(frame.buffer.get(), &frame.video_info, "56.78", 4, 6,
                                                      sf::yuv_color::red));
    EXPECT_TRUE(has_any_byte_different_from(frame.buffer, 0));
}

TEST_F(text_drawer_render_test, draw_text_rejects_empty_text_without_modifying_buffer) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 64, 32, 7);
    ASSERT_TRUE(frame.is_valid);

    EXPECT_FALSE(sf::draw_text_on_nv12_frame(frame.buffer.get(), &frame.video_info, "", 3, 3, sf::yuv_color::red));
    EXPECT_TRUE(all_bytes_equal_to(frame.buffer, 7));
}

TEST_F(text_drawer_render_test, draw_background_rect_i420_draw_char_on_i420_draw_char_on_i420_large_red_paths) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_I420, 96, 48, 0);
    ASSERT_TRUE(frame.is_valid);

    ASSERT_TRUE(sf::draw_text_on_i420_frame(frame.buffer.get(), &frame.video_info, "A1", 2, 2, sf::yuv_color::white));
    ASSERT_TRUE(sf::draw_text_on_i420_frame_large_red(frame.buffer.get(), &frame.video_info, "B2", 8, 8,
                                                      sf::yuv_color::red));
    EXPECT_TRUE(has_any_byte_different_from(frame.buffer, 0));
}

TEST_F(text_drawer_render_test, draw_background_rect_nv12_draw_char_on_nv12_draw_char_on_nv12_large_red_paths) {
    auto frame = create_test_video_buffer(GST_VIDEO_FORMAT_NV12, 96, 48, 0);
    ASSERT_TRUE(frame.is_valid);

    ASSERT_TRUE(sf::draw_text_on_nv12_frame(frame.buffer.get(), &frame.video_info, "A1", 2, 2, sf::yuv_color::white));
    ASSERT_TRUE(sf::draw_text_on_nv12_frame_large_red(frame.buffer.get(), &frame.video_info, "B2", 8, 8,
                                                      sf::yuv_color::red));
    EXPECT_TRUE(has_any_byte_different_from(frame.buffer, 0));
}
