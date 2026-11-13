// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/roi_detection_processing.h"
#include "../../encoder_cli/src/processing/roi_detection_random_access.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_64x64_30.yuv"));
        config.format = sf::video_format::i420;
        config.pack_rois = false;
        return config;
    }
} // namespace

class WorkerGstTasksLifecycleTest : public GStreamerTestFixture {};

TEST_F(WorkerGstTasksLifecycleTest, RoiGstTaskJoinsCleanlyWhenQueueClosedAndStopSet) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);
    data.raw_frame_queue.close();

    bypass_gst_poll_slot roi_slot{};
    ASSERT_TRUE(start_roi_detection_gst_task(data, roi_slot));
    EXPECT_NO_THROW(join_roi_detection_gst_task(data, roi_slot));
}

TEST_F(WorkerGstTasksLifecycleTest, RandomAccessGstTaskNoOpWhenPackRoisDisabled) {
    auto config = make_base_config();
    global_data data(std::move(config));

    bypass_gst_poll_slot ra_slot{};
    ASSERT_TRUE(start_random_access_gst_task(data, ra_slot));
    EXPECT_EQ(ra_slot.task, nullptr);
    join_random_access_gst_task(data, ra_slot);
}
