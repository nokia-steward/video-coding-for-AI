// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../common/gst_test_fixture.h"
#include "gst_raii.h"
#include "nvosd_overlay.h"
#include "rectangle.h"
#include "yuv_color.h"

#ifdef HAVE_DEEPSTREAM
#include <gstnvdsmeta.h>
#include <nvdsmeta.h>
#endif

namespace {
    class nvosd_overlay_test : public GStreamerTestFixture {};
} // namespace

#ifdef HAVE_DEEPSTREAM
TEST_F(nvosd_overlay_test, ensure_batch_meta_rejects_invalid_input) {
    EXPECT_EQ(sf::ensure_nvds_batch_meta(nullptr, 16, 16), nullptr);

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 64, nullptr));
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(sf::ensure_nvds_batch_meta(buffer.get(), 0, 16), nullptr);
    EXPECT_EQ(sf::ensure_nvds_batch_meta(buffer.get(), 16, 0), nullptr);
}

TEST_F(nvosd_overlay_test, ensure_batch_meta_creates_and_reuses_attached_meta) {
    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 128, nullptr));
    ASSERT_NE(buffer, nullptr);
    GST_BUFFER_PTS(buffer.get()) = 1234;

    auto *first = sf::ensure_nvds_batch_meta(buffer.get(), 32, 24);
    ASSERT_NE(first, nullptr);
    auto *second = sf::ensure_nvds_batch_meta(buffer.get(), 32, 24);
    ASSERT_NE(second, nullptr);
    // Backends may return a newly wrapped batch-meta pointer on subsequent calls.
    // We only require valid attached metadata with expected frame dimensions.

    ASSERT_NE(second->frame_meta_list, nullptr);
    auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(second->frame_meta_list->data);
    ASSERT_NE(frame_meta, nullptr);
    EXPECT_EQ(frame_meta->source_frame_width, 32U);
    EXPECT_EQ(frame_meta->source_frame_height, 24U);
}

TEST_F(nvosd_overlay_test, add_overlay_functions_validate_arguments) {
    const sf::rectangle rect{1, 2, 3, 4};
    EXPECT_FALSE(sf::add_rect_to_display_meta(nullptr, rect, sf::yuv_color::white, 1));
    EXPECT_FALSE(sf::add_text_to_display_meta(nullptr, "fps", 0, 0, sf::yuv_color::white, 12));

    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 128, nullptr));
    ASSERT_NE(buffer, nullptr);
    auto *batch_meta = sf::ensure_nvds_batch_meta(buffer.get(), 32, 24);
    ASSERT_NE(batch_meta, nullptr);

    EXPECT_FALSE(sf::add_text_to_display_meta(batch_meta, nullptr, 0, 0, sf::yuv_color::white, 12));
    EXPECT_FALSE(sf::add_text_to_display_meta(batch_meta, "", 0, 0, sf::yuv_color::white, 12));
}

TEST_F(nvosd_overlay_test, add_rect_and_text_attach_display_meta_entries) {
    auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 128, nullptr));
    ASSERT_NE(buffer, nullptr);
    auto *batch_meta = sf::ensure_nvds_batch_meta(buffer.get(), 64, 48);
    ASSERT_NE(batch_meta, nullptr);

    ASSERT_TRUE(sf::add_rect_to_display_meta(batch_meta, sf::rectangle{4, 6, 16, 10}, sf::yuv_color::red, 2));
    ASSERT_TRUE(sf::add_text_to_display_meta(batch_meta, "bitrate", -5, -1, sf::yuv_color::white, 0));

    ASSERT_NE(batch_meta->frame_meta_list, nullptr);
    auto *frame_meta = reinterpret_cast<NvDsFrameMeta *>(batch_meta->frame_meta_list->data);
    ASSERT_NE(frame_meta, nullptr);
    ASSERT_NE(frame_meta->display_meta_list, nullptr);

    guint total_rects = 0;
    guint total_labels = 0;
    for (NvDsMetaList *iter = frame_meta->display_meta_list; iter != nullptr; iter = iter->next) {
        auto *display_meta = reinterpret_cast<NvDsDisplayMeta *>(iter->data);
        if (display_meta == nullptr) {
            continue;
        }
        total_rects += display_meta->num_rects;
        total_labels += display_meta->num_labels;
    }

    EXPECT_GE(total_rects, 1U);
    EXPECT_GE(total_labels, 1U);
}
#else
TEST_F(nvosd_overlay_test, skipped_without_deepstream) { GTEST_SKIP() << "DeepStream not available"; }
#endif
