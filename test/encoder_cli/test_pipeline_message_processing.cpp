// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/pipeline_message_processing.h"
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

    [[nodiscard]] sf::unique_gst_element_ptr make_empty_pipeline(const char *name) {
        return sf::unique_gst_element_ptr(gst_pipeline_new(name));
    }

    [[nodiscard]] bool wait_for_flag(const std::atomic<bool> &flag, const std::chrono::milliseconds timeout) {
        const auto start_time = std::chrono::steady_clock::now();
        while (!flag.load()) {
            if (std::chrono::steady_clock::now() - start_time >= timeout) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }
} // namespace

class PipelineMessageProcessingTest : public GStreamerTestFixture {};

TEST_F(PipelineMessageProcessingTest, InputPipelineEosClosesRawFrameQueue) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.input_pipeline = make_empty_pipeline("test-input-pipeline");
    ASSERT_NE(data.input_pipeline.get(), nullptr);

    std::atomic<bool> pop_finished{false};
    std::jthread pop_thread([&]() {
        const auto popped = data.raw_frame_queue.pop();
        EXPECT_FALSE(popped.has_value());
        pop_finished.store(true);
    });

    std::atomic<bool> message_loop_finished{false};
    std::jthread message_thread([&]() {
        process_pipeline_messages(data.input_pipeline, data);
        message_loop_finished.store(true);
    });

    const bool posted = gst_element_post_message(data.input_pipeline.get(),
                                                 gst_message_new_eos(GST_OBJECT(data.input_pipeline.get())));
    ASSERT_TRUE(posted);

    ASSERT_TRUE(wait_for_flag(message_loop_finished, std::chrono::seconds(2)));
    ASSERT_TRUE(wait_for_flag(pop_finished, std::chrono::seconds(2)));
    EXPECT_FALSE(data.shutdown_started.load());
}

TEST_F(PipelineMessageProcessingTest, EncodingPipelineEosClosesEncodedFrameQueue) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.encoding_pipeline = make_empty_pipeline("test-encoding-pipeline");
    ASSERT_NE(data.encoding_pipeline.get(), nullptr);

    std::atomic<bool> pop_finished{false};
    std::jthread pop_thread([&]() {
        const auto popped = data.encoded_frame_queue.pop();
        EXPECT_FALSE(popped.has_value());
        pop_finished.store(true);
    });

    std::atomic<bool> message_loop_finished{false};
    std::jthread message_thread([&]() {
        process_pipeline_messages(data.encoding_pipeline, data);
        message_loop_finished.store(true);
    });

    const bool posted = gst_element_post_message(data.encoding_pipeline.get(),
                                                 gst_message_new_eos(GST_OBJECT(data.encoding_pipeline.get())));
    ASSERT_TRUE(posted);

    ASSERT_TRUE(wait_for_flag(message_loop_finished, std::chrono::seconds(2)));
    ASSERT_TRUE(wait_for_flag(pop_finished, std::chrono::seconds(2)));
    EXPECT_FALSE(data.shutdown_started.load());
}

TEST_F(PipelineMessageProcessingTest, OutputPipelineErrorTriggersShutdown) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.input_pipeline = make_empty_pipeline("test-input-pipeline");
    data.encoding_pipeline = make_empty_pipeline("test-encoding-pipeline");
    data.output_pipeline = make_empty_pipeline("test-output-pipeline");
    ASSERT_NE(data.output_pipeline.get(), nullptr);

    std::atomic<bool> message_loop_finished{false};
    std::jthread message_thread([&]() {
        process_pipeline_messages(data.output_pipeline, data);
        message_loop_finished.store(true);
    });

    GError *error = g_error_new(GST_CORE_ERROR, GST_CORE_ERROR_FAILED, "synthetic non-display error");
    const bool posted = gst_element_post_message(
            data.output_pipeline.get(),
            gst_message_new_error(GST_OBJECT(data.output_pipeline.get()), error, "synthetic debug info"));
    ASSERT_TRUE(posted);

    ASSERT_TRUE(wait_for_flag(message_loop_finished, std::chrono::seconds(3)));
    EXPECT_TRUE(data.shutdown_started.load());
    EXPECT_TRUE(data.stop_processing.load());
}
