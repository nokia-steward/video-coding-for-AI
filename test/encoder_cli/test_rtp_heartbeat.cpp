// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/rtp_heartbeat.h"
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
        return config;
    }
} // namespace

class RtpHeartbeatTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();
        init_rtp_heartbeat_debug_category();
    }
};

TEST_F(RtpHeartbeatTest, StartHeartbeatNoopsWhenCommunicationHostMissing) {
    auto config = make_base_config();
    config.communication.host.clear();
    config.communication.port = 5000;
    global_data data(std::move(config));
    data.stop_processing.store(true);

    const bool started = start_rtp_heartbeat(data);
    EXPECT_TRUE(started);
    EXPECT_FALSE(data.heartbeat_thread_started.load());
}

TEST_F(RtpHeartbeatTest, StartHeartbeatNoopsWhenCommunicationPortIsZero) {
    auto config = make_base_config();
    config.communication.host = "127.0.0.1";
    config.communication.port = 0;
    global_data data(std::move(config));
    data.stop_processing.store(true);

    const bool started = start_rtp_heartbeat(data);
    EXPECT_TRUE(started);
    EXPECT_FALSE(data.heartbeat_thread_started.load());
}

TEST_F(RtpHeartbeatTest, StartHeartbeatNoopsWhenThreadAlreadyMarkedStarted) {
    auto config = make_base_config();
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    global_data data(std::move(config));
    data.stop_processing.store(true);
    data.heartbeat_thread_started.store(true);

    const bool started = start_rtp_heartbeat(data);
    EXPECT_TRUE(started);
    EXPECT_TRUE(data.heartbeat_thread_started.load());
}
