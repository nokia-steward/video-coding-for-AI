// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../encoder_cli/src/roi/ctu_alignment.h"
#include "../../encoder_cli/src/roi/pri_packer_view.h"
#include "../common/test_debug_categories.h"
#include "../libvcai/include/gst_raii.h"
#include "../libvcai/include/rectangle.h"

/**
 * @file test_pri_packer_view.cpp
 * @brief Unit tests for the PRI packer narrow view interface.
 *
 * Exercises the pri_packer_view seam directly, without constructing a full
 * global_data object (per the PRD testing decision).
 */

namespace {
    sf::unique_gst_sample_ptr create_test_sample(int width, int height) {
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
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
        const gsize buffer_size = video_info.size;
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, buffer_size, nullptr);
        if (!buffer) {
            gst_caps_unref(caps);
            return nullptr;
        }
        gst_buffer_memset(buffer, 0, 0, buffer_size);
        GstSample *sample = gst_sample_new(buffer, caps, nullptr, nullptr);
        gst_buffer_unref(buffer);
        gst_caps_unref(caps);
        return sf::unique_gst_sample_ptr(sample);
    }
} // namespace

class PriPackerViewTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
        init_all_test_debug_categories();
    }
};

TEST_F(PriPackerViewTest, LowDelayDispatchesToPacker) {
    pri_packer_storage storage = std::make_unique<low_delay_packer>(1024, 1024, default_ctu_size, 3);
    pri_packer_view view(storage);

    EXPECT_TRUE(view.is_low_delay());
    EXPECT_FALSE(view.is_random_access());

    const std::vector<std::pair<sf::rectangle, std::string>> rects = {{sf::rectangle(0, 0, 100, 50), "obj1"}};
    const auto result = view.pack_low_delay(rects, 1);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->packed_rectangles.empty());

    // Random-access operations are safe no-ops on a low-delay packer.
    EXPECT_FALSE(view.get_random_access_period().has_value());
    view.add_random_access_frame(1, create_test_sample(100, 100), {});
    view.close_random_access();
}

TEST_F(PriPackerViewTest, RandomAccessDispatchesToPacker) {
    pri_packer_storage storage = std::make_unique<random_access_packer>(1024, 1024, default_ctu_size, 30, 3);
    pri_packer_view view(storage);

    EXPECT_FALSE(view.is_low_delay());
    EXPECT_TRUE(view.is_random_access());

    // Low-delay packing is a no-op on a random-access packer.
    EXPECT_FALSE(view.pack_low_delay({}, 1).has_value());

    const std::vector<std::pair<sf::rectangle, std::string>> rois = {{sf::rectangle(10, 10, 50, 50), "obj1"}};
    for (int frame = 1; frame <= 60; ++frame) {
        view.add_random_access_frame(frame, create_test_sample(100, 100), rois);
    }

    const auto period = view.get_random_access_period();
    ASSERT_TRUE(period.has_value());
    EXPECT_EQ(period->frames.size(), 30u);
    EXPECT_FALSE(period->processed_rois.empty());

    view.close_random_access();
    EXPECT_FALSE(view.get_random_access_period().has_value());
}
