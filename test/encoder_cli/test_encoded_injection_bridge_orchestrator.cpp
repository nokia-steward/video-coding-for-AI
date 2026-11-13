// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/bridge/context/encoded_injection_bridge_context.h"
#include "../../encoder_cli/src/bridge/orchestration/encoded_injection_bridge_orchestrator.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.packer_type_value = packer_type::low_delay;
        config.num_frames = 1;
        return config;
    }

    [[nodiscard]] sf::unique_gst_buffer_ptr make_test_buffer() {
        auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 8, nullptr));
        if (!buffer) {
            return nullptr;
        }
        GstMapInfo map_info{};
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_WRITE)) {
            return nullptr;
        }
        map_info.data[0] = 0x00;
        map_info.data[1] = 0x00;
        map_info.data[2] = 0x01;
        map_info.data[3] = 0x02;
        map_info.data[4] = 0x01;
        map_info.data[5] = 0xAA;
        map_info.data[6] = 0xBB;
        map_info.data[7] = 0xCC;
        gst_buffer_unmap(buffer.get(), &map_info);
        return buffer;
    }
} // namespace

class EncodedInjectionBridgeOrchestratorTest : public GStreamerTestFixture {};

TEST_F(EncodedInjectionBridgeOrchestratorTest, FrameSkipPathAdvancesCounter) {
    global_data data(make_config());
    auto context = encoded_injection_bridge_context{.ports = &data.encoded_ports,
                                                    .egress_runtime = &data.egress_bridge_runtime,
                                                    .metadata_runtime = &data.metadata_runtime,
                                                    .packed_regions_queue = &data.packed_regions_queue,
                                                    .memory_runtime = &data.memory_runtime,
                                                    .params = &data.params,
                                                    .host = &data,
                                                    .frame_counter = 0,
                                                    .frame_skip = 1};
    auto buffer = make_test_buffer();
    ASSERT_NE(buffer, nullptr);
    data.packed_regions_queue.push({.sequence_id = 1, .regions = sf::packed_regions_info::create_sentinel()});

    const auto result = encoded_injection_process_frame_via_orchestrator(context, buffer.get(),
                                                                         encoded_frame_luma_adaptation_state{}, 1);
    EXPECT_EQ(context.frame_counter, 1U);
    EXPECT_NE(result.status, encoded_bridge_step_status::drain_complete);
}
