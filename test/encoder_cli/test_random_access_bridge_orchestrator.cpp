// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/bridge/app/random_access_app_adapter.h"
#include "../../encoder_cli/src/bridge/context/random_access_bridge_context.h"
#include "../../encoder_cli/src/bridge/orchestration/random_access_bridge_orchestrator.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_low_delay_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.packer_type_value = packer_type::low_delay;
        config.num_frames = 1;
        return config;
    }
} // namespace

class RandomAccessBridgeOrchestratorTest : public GStreamerTestFixture {};

TEST_F(RandomAccessBridgeOrchestratorTest, MissingRandomAccessPackerTriggersFailFastStop) {
    global_data data(make_low_delay_config());
    data.stop_processing = false;

    auto context = random_access_bridge_context{.data = &data, .packer = nullptr};
    const random_access_app_adapter adapter(data);
    EXPECT_EQ(random_access_get_packer_via_orchestrator(context, adapter), nullptr);
    EXPECT_TRUE(data.stop_processing.load());
}
