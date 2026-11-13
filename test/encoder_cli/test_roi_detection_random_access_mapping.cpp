// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/roi/random_access_packer.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] sf::unique_gst_sample_ptr create_test_sample(const int width, const int height) {
        GstVideoInfo video_info;
        gst_video_info_init(&video_info);
        if (!gst_video_info_set_format(&video_info, GST_VIDEO_FORMAT_I420, static_cast<guint>(width),
                                       static_cast<guint>(height))) {
            return nullptr;
        }

        GstCaps *caps = gst_video_info_to_caps(&video_info);
        if (!caps) {
            return nullptr;
        }

        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, video_info.size, nullptr);
        if (!buffer) {
            gst_caps_unref(caps);
            return nullptr;
        }
        gst_buffer_memset(buffer, 0, 0, video_info.size);

        GstSample *sample = gst_sample_new(buffer, caps, nullptr, nullptr);
        gst_buffer_unref(buffer);
        gst_caps_unref(caps);
        return sf::unique_gst_sample_ptr(sample);
    }
} // namespace

class RoiDetectionRandomAccessMappingTest : public GStreamerTestFixture {};

TEST_F(RoiDetectionRandomAccessMappingTest, MappingIndicesRemainValidWhenRoisMerge) {
    random_access_packer packer(320, 240, 64, 2, 3);

    const std::vector<std::pair<sf::rectangle, std::string>> overlapping_rois = {
            {sf::rectangle(10, 10, 60, 60), "obj_1"},
            {sf::rectangle(20, 20, 60, 60), "obj_2"},
    };

    for (int frame_number = 1; frame_number <= 4; ++frame_number) {
        auto sample = create_test_sample(320, 240);
        ASSERT_NE(sample, nullptr);
        packer.add_frame(frame_number, std::move(sample), overlapping_rois);
    }

    packer.close();
    const auto period_opt = packer.get_complete_period();
    ASSERT_TRUE(period_opt.has_value());

    const auto &period = *period_opt;
    ASSERT_FALSE(period.object_ids.empty());
    ASSERT_FALSE(period.processed_rois.empty());
    ASSERT_EQ(period.original_to_packed_mapping.size(), period.object_ids.size());

    for (const size_t packed_index: period.original_to_packed_mapping) {
        EXPECT_LT(packed_index, period.processed_rois.size());
    }

    // Overlapping objects should merge into one packed ROI.
    EXPECT_EQ(period.processed_rois.size(), 1U);
    EXPECT_EQ(period.original_to_packed_mapping[0], period.original_to_packed_mapping[1]);
}

TEST_F(RoiDetectionRandomAccessMappingTest, FramesKeepSourceRoisWhenPackingUsesExpandedRectangles) {
    random_access_packer packer(320, 240, 64, 2, 3);

    const std::vector<std::pair<sf::rectangle, std::string>> source_rois = {
            {sf::rectangle(40, 40, 30, 30), "obj_placement"},
    };
    const std::vector<std::pair<sf::rectangle, std::string>> packing_rois = {
            {sf::rectangle(20, 20, 70, 70), "obj_placement"},
    };

    for (int frame_number = 1; frame_number <= 4; ++frame_number) {
        auto sample = create_test_sample(320, 240);
        ASSERT_NE(sample, nullptr);
        packer.add_frame(frame_number, std::move(sample), source_rois, packing_rois);
    }

    packer.close();
    const auto period_opt = packer.get_complete_period();
    ASSERT_TRUE(period_opt.has_value());
    const auto &period = *period_opt;

    ASSERT_FALSE(period.frames.empty());
    ASSERT_FALSE(period.frames[0].rectangles_with_ids.empty());
    EXPECT_EQ(period.frames[0].rectangles_with_ids[0].first, source_rois[0].first);

    ASSERT_FALSE(period.original_rois.empty());
    EXPECT_EQ(period.original_rois[0], packing_rois[0].first);
}
