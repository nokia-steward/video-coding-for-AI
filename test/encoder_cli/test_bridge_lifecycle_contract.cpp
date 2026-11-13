// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../libvcai/include/gst_raii.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::camera;
        config.width = 16;
        config.height = 16;
        config.framerate = 30;
        config.format = sf::video_format::i420;
        return config;
    }
} // namespace

TEST(BridgeLifecycleContractTest, QueueClosePathIsIdempotentAcrossIngressAndEgress) {
    global_data data(make_base_config());
    data.push_pending_encoded_contract_state(1, {.enabled = true, .downscale_factor = 0.8});

    data.close_raw_queue_once();
    data.close_raw_queue_once();
    EXPECT_TRUE(data.ingress_runtime.raw_queue_closed.load());
    EXPECT_FALSE(data.raw_frame_queue.pop().has_value());

    data.close_encoded_queue_once();
    data.close_encoded_queue_once();
    EXPECT_TRUE(data.ingress_runtime.encoded_queue_closed.load());
    EXPECT_FALSE(data.pop_pending_encoded_contract_state().has_value());
    EXPECT_FALSE(data.encoded_frame_queue.pop().has_value());

    data.close_metadata_queues_once();
    data.close_metadata_queues_once();
    EXPECT_TRUE(data.egress_bridge_runtime.metadata_queues_closed.load());
    EXPECT_FALSE(data.packed_regions_queue.pop().has_value());
    EXPECT_FALSE(data.display_regions_queue.pop().has_value());
    EXPECT_FALSE(data.debug_regions_queue.pop().has_value());
}

TEST(BridgeLifecycleContractTest, ResetProcessingStateClearsCapsLocksAndLifecycleFlags) {
    global_data data(make_base_config());
    const sf::unique_gst_caps_ptr startup_caps(gst_caps_new_simple(
            "video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr));
    ASSERT_TRUE(startup_caps);

    ASSERT_TRUE(data.egress_bridge_runtime.mark_startup_configured_once(true));
    ASSERT_TRUE(data.egress_bridge_runtime.mark_startup_configured_once(false));
    ASSERT_TRUE(data.egress_bridge_runtime.lock_caps_contract(true, startup_caps.get()));
    ASSERT_TRUE(data.egress_bridge_runtime.lock_caps_contract(false, startup_caps.get()));
    data.egress_bridge_runtime.encoding_eos_sent = true;
    data.egress_bridge_runtime.sei_eos_sent = true;

    data.close_raw_queue_once();
    data.close_encoded_queue_once();
    data.close_metadata_queues_once();
    EXPECT_TRUE(data.ingress_runtime.raw_queue_closed.load());
    EXPECT_TRUE(data.ingress_runtime.encoded_queue_closed.load());
    EXPECT_TRUE(data.egress_bridge_runtime.encoding_caps_locked.load());
    EXPECT_TRUE(data.egress_bridge_runtime.sei_caps_locked.load());

    data.reset_processing_state();

    EXPECT_FALSE(data.stop_processing.load());
    EXPECT_FALSE(data.ingress_runtime.raw_queue_closed.load());
    EXPECT_FALSE(data.ingress_runtime.encoded_queue_closed.load());
    EXPECT_FALSE(data.egress_bridge_runtime.encoding_eos_sent.load());
    EXPECT_FALSE(data.egress_bridge_runtime.sei_eos_sent.load());
    EXPECT_FALSE(data.egress_bridge_runtime.encoding_caps_locked.load());
    EXPECT_FALSE(data.egress_bridge_runtime.sei_caps_locked.load());
    EXPECT_FALSE(data.egress_bridge_runtime.is_startup_configured(true));
    EXPECT_FALSE(data.egress_bridge_runtime.is_startup_configured(false));
    EXPECT_FALSE(data.egress_bridge_runtime.metadata_queues_closed.load());
}
