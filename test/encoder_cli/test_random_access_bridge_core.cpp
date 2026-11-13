// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/bridge/core/random_access_bridge_core.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_random_access_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.packer_type_value = packer_type::random_access;
        config.num_frames = 1;
        return config;
    }
} // namespace

class RandomAccessBridgeCoreTest : public GStreamerTestFixture {};

TEST_F(RandomAccessBridgeCoreTest, ProcessPeriodReturnsOkForEmptyPeriod) {
    global_data data(make_random_access_config());
    random_access_packer::complete_period period{};
    period.period_number = 0;
    period.period_start_frame = 0;

    GstClockTime next_pts = GST_CLOCK_TIME_NONE;
    auto last_push_time = std::chrono::steady_clock::now();
    random_access_bridge_core::media_session session{data};
    const auto result =
            random_access_bridge_core::process_period(session, period, GST_SECOND / 30, next_pts, last_push_time);
    EXPECT_EQ(result.status, random_access_bridge_core::step_status::ok);
}

TEST_F(RandomAccessBridgeCoreTest, CleanupReturnsFatalForNullPacker) {
    global_data data(make_random_access_config());
    random_access_bridge_core::media_session session{data};
    const auto result = random_access_bridge_core::cleanup(session, nullptr);
    EXPECT_EQ(result.status, random_access_bridge_core::step_status::fatal_stop);
}
