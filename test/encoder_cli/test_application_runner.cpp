// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>

#include "../../encoder_cli/src/application/application_runner.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }
} // namespace

class ApplicationRunnerTest : public GStreamerTestFixture {};

TEST_F(ApplicationRunnerTest, RunApplicationReturnsWhenNoInputModeMatches) {
    auto config = make_base_config();
    config.input_type_value = static_cast<input_type>(999);
    global_data data(std::move(config));

    EXPECT_NO_THROW(run_application(data));
}

TEST_F(ApplicationRunnerTest, RunApplicationUsesUdpModeAndExitsWhenStopAlreadyRequested) {
    auto config = make_base_config();
    config.input_type_value = input_type::udp;
    global_data data(std::move(config));
    data.stop_processing.store(true);

    const auto started_at = std::chrono::steady_clock::now();
    run_application(data);
    const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at)
                    .count();

    EXPECT_LT(elapsed_ms, 250);
    EXPECT_TRUE(data.stop_processing.load());
}
