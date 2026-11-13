// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>

#include "../../encoder_cli/src/encoder_config.h"
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

TEST(EncoderSubmissionQueueTest, CloseIsIdempotentAcrossLifecycle) {
    global_data data(make_base_config());

    data.encoder_submission_queue.push({.buffer = nullptr,
                                        .luma_adaptation = {.enabled = true, .downscale_factor = 0.8},
                                        .contract_sequence_id = 7});

    data.close_encoder_submission_queue_once();
    data.close_encoder_submission_queue_once();
    EXPECT_TRUE(data.encoder_submission_queue_closed.load());
    // Closed exactly once: the queued item drains, then the queue reports empty.
    EXPECT_TRUE(data.encoder_submission_queue.pop().has_value());
    EXPECT_FALSE(data.encoder_submission_queue.pop().has_value());
}

TEST(EncoderSubmissionQueueTest, DrainToEmptyAfterClose) {
    global_data data(make_base_config());

    for (uint64_t i = 0; i < 5; ++i) {
        data.encoder_submission_queue.push({.buffer = nullptr, .luma_adaptation = {}, .contract_sequence_id = i});
    }

    data.close_encoder_submission_queue_once();

    uint64_t drained = 0;
    while (data.encoder_submission_queue.pop().has_value()) {
        ++drained;
    }
    EXPECT_EQ(drained, 5u);
    EXPECT_FALSE(data.encoder_submission_queue.pop().has_value());
}

TEST(EncoderSubmissionQueueTest, WaitForPendingContractDrainHonoringStop) {
    global_data data(make_base_config());
    data.push_pending_encoded_contract_state(1, {.enabled = true, .downscale_factor = 0.8});

    // With a pending state and stop requested, honoring stop returns false immediately.
    data.stop_processing = true;
    EXPECT_FALSE(data.wait_for_pending_encoded_contract_drain(std::chrono::milliseconds(50), true));

    // Ignoring stop keeps waiting; still returns false on timeout while a state is pending.
    EXPECT_FALSE(data.wait_for_pending_encoded_contract_drain(std::chrono::milliseconds(50), false));

    // Once consumed, the drain helper reports drained even when honoring the stop request.
    data.stop_processing = false;
    EXPECT_TRUE(data.pop_pending_encoded_contract_state().has_value());
    EXPECT_TRUE(data.wait_for_pending_encoded_contract_drain(std::chrono::milliseconds(50), true));
    EXPECT_FALSE(data.has_pending_encoded_contract_states());
}

TEST(EncoderSubmissionQueueTest, ResetProcessingStateReopensSubmissionQueue) {
    global_data data(make_base_config());
    data.close_encoder_submission_queue_once();
    EXPECT_TRUE(data.encoder_submission_queue_closed.load());

    data.reset_processing_state();
    EXPECT_FALSE(data.encoder_submission_queue_closed.load());
    data.encoder_submission_queue.push({.buffer = nullptr, .luma_adaptation = {}, .contract_sequence_id = 1});
    data.close_encoder_submission_queue_once();
    EXPECT_TRUE(data.encoder_submission_queue.pop().has_value());
    EXPECT_FALSE(data.encoder_submission_queue.pop().has_value());
}
