// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/udp_mode.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::udp;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.udp.port = 5004;
        config.udp.bind_address = "0.0.0.0";
        config.udp.encoding = "H265";
        config.udp.payload_type = 96;
        config.udp.clock_rate = 90000;
        config.udp.latency_ms = 200;
        return config;
    }
} // namespace

class UdpModeTest : public GStreamerTestFixture {};

TEST_F(UdpModeTest, ReturnsImmediatelyWhenStopAlreadyRequested) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);

    const auto started_at = std::chrono::steady_clock::now();
    run_udp_mode(data);
    const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at)
                    .count();

    EXPECT_LT(elapsed_ms, 250);
}

TEST_F(UdpModeTest, PreservesStopFlagWhenAlreadySet) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);

    run_udp_mode(data);
    EXPECT_TRUE(data.stop_processing.load());
}

TEST_F(UdpModeTest, DoesNotMarkShutdownStartedOnImmediateReturn) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);
    data.shutdown_started.store(false);

    run_udp_mode(data);
    EXPECT_FALSE(data.shutdown_started.load());
}
