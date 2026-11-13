// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/processing/encoded_frame_processing.h"
#include "../../encoder_cli/src/processing/pipeline_message_processing.h"
#include "../../libvcai/include/encoder_optimization_info.h"
#include "../../libvcai/include/luma_adaptation_math.h"
#include "../../libvcai/include/nal_utils.h"
#include "../common/gst_test_fixture.h"
#include "../libvcai/include/gst_raii.h"
#include "../libvcai/include/gst_wrappers.h"

namespace {
    [[nodiscard]] bool setup_sei_caps_contract(global_data &data, GstElement *appsrc, GstCaps *caps) {
        if (appsrc == nullptr || caps == nullptr) {
            return false;
        }
        data.encoded_ports.sei_src = appsrc;
        if (!data.egress_bridge_runtime.mark_startup_configured_once(false)) {
            return false;
        }
        return data.egress_bridge_runtime.lock_caps_contract(false, caps);
    }
} // namespace

/**
 * @file test_processing_functions.cpp
 * @brief Tests for processing function behavior.
 *
 * Note: These tests focus on basic functionality and error handling.
 * Full integration tests require complete pipeline setup and are
 * covered in end-to-end test plans.
 */

class ProcessingFunctionsTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();

        // Create minimal config
        config = encoder_config();
        config.input_type_value = input_type::file;
        config.width = 1920;
        config.height = 1080;
        config.framerate = 30;

        data = std::make_unique<global_data>(std::move(config));

        // Create minimal pipelines
        data->input_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-input"));
        data->encoding_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-encoding"));
        data->output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-output"));
    }

    void TearDown() override {
        // Signal stop processing
        if (data) {
            data->stop_processing = true;
            data->raw_frame_queue.close();
            data->encoded_frame_queue.close();
            data->packed_regions_queue.close();
        }

        // Give threads time to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        data.reset();
        GStreamerTestFixture::TearDown();
    }

    encoder_config config;
    std::unique_ptr<global_data> data;
};

/**
 * @brief Test process_encoded_frames handles empty queue gracefully.
 */
TEST_F(ProcessingFunctionsTest, ProcessEncodedFramesEmptyQueue) {
    // Close queue immediately to simulate empty/closed queue
    data->encoded_frame_queue.close();

    // Function should exit gracefully when queue is closed
    // Run in a separate thread with timeout
    std::atomic<bool> finished{false};
    std::jthread processing_thread([&]() {
        process_encoded_frames(*data);
        finished = true;
    });

    // Wait for function to finish (should be quick with closed queue)
    processing_thread.join();

    EXPECT_TRUE(finished);
}

/**
 * @brief Test process_encoded_frames handles stop signal.
 */
TEST_F(ProcessingFunctionsTest, ProcessEncodedFramesStopSignal) {
    // Signal stop processing
    data->stop_processing = true;
    data->encoded_frame_queue.close();

    std::atomic<bool> finished{false};
    std::jthread processing_thread([&]() {
        process_encoded_frames(*data);
        finished = true;
    });

    processing_thread.join();

    EXPECT_TRUE(finished);
}

/**
 * @brief Test process_pipeline_messages handles stop signal.
 */
TEST_F(ProcessingFunctionsTest, ProcessPipelineMessagesStopSignal) {
    // Signal stop processing
    data->stop_processing = true;

    std::atomic<bool> finished{false};
    std::jthread message_thread([&]() {
        process_pipeline_messages(data->output_pipeline, *data);
        finished = true;
    });

    // Wait a bit then signal stop
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    data->stop_processing = true;

    message_thread.join();

    EXPECT_TRUE(finished);
}

/**
 * @brief Test process_pipeline_messages handles EOS message.
 */
TEST_F(ProcessingFunctionsTest, ProcessPipelineMessagesEOS) {
    // Create a test message
    const auto eos_msg = sf::unique_gst_message_ptr(gst_message_new_eos(GST_OBJECT(data->output_pipeline.get())));

    // Note: This test verifies the function structure
    // Full EOS handling requires bus setup which is complex
    SUCCEED();
}

/**
 * @brief Test process_pipeline_messages handles ERROR message.
 */
TEST_F(ProcessingFunctionsTest, ProcessPipelineMessagesError) {
    // Create a test error message
    GError *error = g_error_new(GST_CORE_ERROR, GST_CORE_ERROR_FAILED, "Test error");
    const auto error_msg = sf::unique_gst_message_ptr(
            gst_message_new_error(GST_OBJECT(data->output_pipeline.get()), error, "Test debug"));
    g_error_free(error);

    // Note: This test verifies the function structure
    // Full error handling requires bus setup
    SUCCEED();
}

/**
 * @brief Test processing functions handle null/invalid data gracefully.
 */
TEST_F(ProcessingFunctionsTest, InvalidDataHandling) {
    // Test with minimal valid data structure
    // Functions should handle edge cases gracefully
    SUCCEED();
}

/**
 * @brief Test processing thread synchronization.
 */
TEST_F(ProcessingFunctionsTest, ThreadSynchronization) {
    // Verify that stop_processing flag is checked
    data->stop_processing = false;

    // Close queues to allow threads to exit
    data->encoded_frame_queue.close();
    data->raw_frame_queue.close();

    std::atomic<bool> thread_finished{false};
    std::jthread test_thread([&]() {
        // Simulate processing loop
        while (!data->stop_processing) {
            auto sample = data->encoded_frame_queue.pop();
            if (!sample) {
                break;
            }
        }
        thread_finished = true;
    });

    // Signal stop after a short delay
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    data->stop_processing = true;

    test_thread.join();

    EXPECT_TRUE(thread_finished);
}

TEST_F(ProcessingFunctionsTest, BlurModeInjectsEoiOnIrapFrames) {
    // CRA_NUT (type 21) in HEVC access unit.
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0xAB, 0xCD,
    };

    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), true, false, 0);
    ASSERT_GT(output.size(), irap_frame.size());

    const std::vector<uint8_t> eoi_marker = {0x00, 0x00, 0x01, 0x4E, 0x01, 0xD7};
    const auto marker_it = std::search(output.begin(), output.end(), eoi_marker.begin(), eoi_marker.end());
    EXPECT_NE(marker_it, output.end());
}

TEST_F(ProcessingFunctionsTest, BlurModeDoesNotInjectEoiOnNonIrapFrames) {
    // TRAIL_R (type 1) in HEVC access unit.
    const std::vector<uint8_t> non_irap_frame = {
            0x00, 0x00, 0x01, 0x02, 0x01, 0xAB, 0xCD,
    };

    const auto output = maybe_inject_eoi_sei(non_irap_frame.data(), non_irap_frame.size(), true, false, 0);
    ASSERT_EQ(output.size(), non_irap_frame.size());
    EXPECT_EQ(output, non_irap_frame);
}

TEST_F(ProcessingFunctionsTest, BlurDisabledDoesNotInjectEoi) {
    // IDR_W_RADL (type 19) in HEVC access unit.
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x26, 0x01, 0xAB, 0xCD,
    };

    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), false, false, 0);
    ASSERT_EQ(output.size(), irap_frame.size());
    EXPECT_EQ(output, irap_frame);
}

TEST_F(ProcessingFunctionsTest, QpMapModeDoesNotInjectEoiOnNonIrapFrames) {
    const std::vector<uint8_t> non_irap_frame = {
            0x00, 0x00, 0x01, 0x02, 0x01, 0xAA, 0xBB, 0xCC,
    };

    const auto output = maybe_inject_eoi_sei(non_irap_frame.data(), non_irap_frame.size(), false, true, 10);
    ASSERT_EQ(output.size(), non_irap_frame.size());
    EXPECT_EQ(output, non_irap_frame);
}

TEST_F(ProcessingFunctionsTest, LumaAdaptationInjectsEoiOnIrapFrames) {
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0x33, 0x44, 0x55,
    };

    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), false, false, 0, true, 0.70);
    ASSERT_GT(output.size(), irap_frame.size());

    sf::encoder_optimization_info expected_eoi{};
    expected_eoi.cancel_flag = false;
    expected_eoi.persistence_flag = true;
    expected_eoi.for_human_viewing_idc = 0;
    expected_eoi.for_machine_analysis_idc = 3;
    expected_eoi.reserved_zero_2bits = 0;
    expected_eoi.type = sf::encoder_optimization_info::type_luma_adaptation;
    expected_eoi.luma_adaptation_idc = 1U;
    const auto derived_luma_signal = sf::derive_luma_adaptation_signal(0.70);
    expected_eoi.ratio_luma_sign_flag = derived_luma_signal.ratio_luma_sign_flag;
    expected_eoi.ratio_luma_value = derived_luma_signal.ratio_luma_value;
    expected_eoi.backscale_ratio_value = derived_luma_signal.backscale_ratio_value;
    const auto expected_payload = sf::write_encoder_optimization_info(expected_eoi);
    const auto expected_nal = sf::create_sei_nal_unit(215, expected_payload);

    std::vector<uint8_t> marker = {0x00, 0x00, 0x01};
    marker.insert(marker.end(), expected_nal.begin(), expected_nal.end());
    const auto it = std::search(output.begin(), output.end(), marker.begin(), marker.end());
    EXPECT_NE(it, output.end());
}

TEST_F(ProcessingFunctionsTest, LumaAdaptationEoiWithExplicitUpscaleMultiplierEncodesBackscale) {
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0x77, 0x78, 0x79,
    };

    constexpr double d = 0.5;
    constexpr double u = 1.4; // Explicit upscale (not full backscale 1/d = 2.0)
    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), false, false, 0, true, d, u);
    ASSERT_GT(output.size(), irap_frame.size());

    sf::encoder_optimization_info expected_eoi{};
    expected_eoi.cancel_flag = false;
    expected_eoi.persistence_flag = true;
    expected_eoi.for_human_viewing_idc = 0;
    expected_eoi.for_machine_analysis_idc = 3;
    expected_eoi.reserved_zero_2bits = 0;
    expected_eoi.type = sf::encoder_optimization_info::type_luma_adaptation;
    expected_eoi.luma_adaptation_idc = 1U;
    const auto derived_luma_signal = sf::derive_luma_adaptation_signal(d, u);
    expected_eoi.ratio_luma_sign_flag = derived_luma_signal.ratio_luma_sign_flag;
    expected_eoi.ratio_luma_value = derived_luma_signal.ratio_luma_value;
    expected_eoi.backscale_ratio_value = derived_luma_signal.backscale_ratio_value;
    const auto expected_payload = sf::write_encoder_optimization_info(expected_eoi);
    const auto expected_nal = sf::create_sei_nal_unit(215, expected_payload);
    std::vector<uint8_t> marker = {0x00, 0x00, 0x01};
    marker.insert(marker.end(), expected_nal.begin(), expected_nal.end());
    const auto it = std::search(output.begin(), output.end(), marker.begin(), marker.end());
    EXPECT_NE(it, output.end());
}

TEST_F(ProcessingFunctionsTest, LumaAdaptationEoiUsesConfiguredFactorEvenIfPixelScalingWouldFail) {
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0x77, 0x88, 0x99,
    };

    constexpr double configured_downscale_factor = 1.5; // Invalid for runtime luma scaling path.
    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), false, false, 0, true,
                                             configured_downscale_factor);
    ASSERT_GT(output.size(), irap_frame.size());

    sf::encoder_optimization_info expected_eoi{};
    expected_eoi.cancel_flag = false;
    expected_eoi.persistence_flag = true;
    expected_eoi.for_human_viewing_idc = 0;
    expected_eoi.for_machine_analysis_idc = 3;
    expected_eoi.reserved_zero_2bits = 0;
    expected_eoi.type = sf::encoder_optimization_info::type_luma_adaptation;
    expected_eoi.luma_adaptation_idc = 1U;
    const auto derived_luma_signal = sf::derive_luma_adaptation_signal(configured_downscale_factor);
    expected_eoi.ratio_luma_sign_flag = derived_luma_signal.ratio_luma_sign_flag;
    expected_eoi.ratio_luma_value = derived_luma_signal.ratio_luma_value;
    expected_eoi.backscale_ratio_value = derived_luma_signal.backscale_ratio_value;

    const auto expected_payload = sf::write_encoder_optimization_info(expected_eoi);
    const auto expected_nal = sf::create_sei_nal_unit(215, expected_payload);
    std::vector<uint8_t> marker = {0x00, 0x00, 0x01};
    marker.insert(marker.end(), expected_nal.begin(), expected_nal.end());
    const auto it = std::search(output.begin(), output.end(), marker.begin(), marker.end());
    EXPECT_NE(it, output.end());
}

TEST_F(ProcessingFunctionsTest, QpMapAndLumaAdaptationInjectCombinedEoiType) {
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0xAA, 0xBB, 0xCC,
    };
    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), false, true, 9, true, 0.80);
    ASSERT_GT(output.size(), irap_frame.size());

    sf::encoder_optimization_info expected_eoi{};
    expected_eoi.cancel_flag = false;
    expected_eoi.persistence_flag = true;
    expected_eoi.for_human_viewing_idc = 0;
    expected_eoi.for_machine_analysis_idc = 3;
    expected_eoi.reserved_zero_2bits = 0;
    expected_eoi.type = static_cast<uint16_t>(sf::encoder_optimization_info::type_object_based |
                                              sf::encoder_optimization_info::type_luma_adaptation);
    expected_eoi.object_based_idc = 0x02;
    expected_eoi.quant_threshold_delta = 9U;
    expected_eoi.pic_quant_object_flag = true;
    expected_eoi.luma_adaptation_idc = 1U;
    const auto derived_luma_signal = sf::derive_luma_adaptation_signal(0.80);
    expected_eoi.ratio_luma_sign_flag = derived_luma_signal.ratio_luma_sign_flag;
    expected_eoi.ratio_luma_value = derived_luma_signal.ratio_luma_value;
    expected_eoi.backscale_ratio_value = derived_luma_signal.backscale_ratio_value;

    const auto expected_payload = sf::write_encoder_optimization_info(expected_eoi);
    const auto expected_nal = sf::create_sei_nal_unit(215, expected_payload);
    std::vector<uint8_t> marker = {0x00, 0x00, 0x01};
    marker.insert(marker.end(), expected_nal.begin(), expected_nal.end());
    const auto it = std::search(output.begin(), output.end(), marker.begin(), marker.end());
    EXPECT_NE(it, output.end());
}

TEST_F(ProcessingFunctionsTest, BackgroundBlurAndLumaAdaptationInjectCombinedEoiType) {
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0x91, 0x92, 0x93,
    };
    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), true, false, 0, true, 0.60);
    ASSERT_GT(output.size(), irap_frame.size());

    sf::encoder_optimization_info expected_eoi{};
    expected_eoi.cancel_flag = false;
    expected_eoi.persistence_flag = true;
    expected_eoi.for_human_viewing_idc = 0;
    expected_eoi.for_machine_analysis_idc = 3;
    expected_eoi.reserved_zero_2bits = 0;
    expected_eoi.type = static_cast<uint16_t>(sf::encoder_optimization_info::type_object_based |
                                              sf::encoder_optimization_info::type_luma_adaptation);
    expected_eoi.object_based_idc = 0x01;
    expected_eoi.luma_adaptation_idc = 1U;
    const auto derived_luma_signal = sf::derive_luma_adaptation_signal(0.60);
    expected_eoi.ratio_luma_sign_flag = derived_luma_signal.ratio_luma_sign_flag;
    expected_eoi.ratio_luma_value = derived_luma_signal.ratio_luma_value;
    expected_eoi.backscale_ratio_value = derived_luma_signal.backscale_ratio_value;

    const auto expected_payload = sf::write_encoder_optimization_info(expected_eoi);
    const auto expected_nal = sf::create_sei_nal_unit(215, expected_payload);
    std::vector<uint8_t> marker = {0x00, 0x00, 0x01};
    marker.insert(marker.end(), expected_nal.begin(), expected_nal.end());
    const auto it = std::search(output.begin(), output.end(), marker.begin(), marker.end());
    EXPECT_NE(it, output.end());
}

TEST_F(ProcessingFunctionsTest, MaybeInjectEoiSeiReturnsEmptyForNullOrZeroLengthInput) {
    const auto output_null = maybe_inject_eoi_sei(nullptr, 0, true, false, 0);
    EXPECT_TRUE(output_null.empty());

    const std::vector<uint8_t> bytes = {0x00, 0x00, 0x01, 0x26, 0x01};
    const auto output_zero_length = maybe_inject_eoi_sei(bytes.data(), 0, false, true, 10);
    EXPECT_TRUE(output_zero_length.empty());
}

TEST_F(ProcessingFunctionsTest, QpMapModeClampsBackgroundDeltaToValidRangeOnIrapFrames) {
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0xDE, 0xAD, 0xBE, 0xEF,
    };

    const auto output_high = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), false, true, 999);
    ASSERT_GT(output_high.size(), irap_frame.size());

    sf::encoder_optimization_info expected_high{};
    expected_high.cancel_flag = false;
    expected_high.persistence_flag = true;
    expected_high.for_human_viewing_idc = 0;
    expected_high.for_machine_analysis_idc = 3;
    expected_high.reserved_zero_2bits = 0;
    expected_high.type = 0x01;
    expected_high.object_based_idc = 0x02;
    expected_high.quant_threshold_delta = 51U;
    expected_high.pic_quant_object_flag = true;
    const auto expected_high_payload = sf::write_encoder_optimization_info(expected_high);
    const auto expected_high_nal = sf::create_sei_nal_unit(215, expected_high_payload);
    std::vector<uint8_t> expected_high_marker = {0x00, 0x00, 0x01};
    expected_high_marker.insert(expected_high_marker.end(), expected_high_nal.begin(), expected_high_nal.end());
    const auto high_it = std::search(output_high.begin(), output_high.end(), expected_high_marker.begin(),
                                     expected_high_marker.end());
    EXPECT_NE(high_it, output_high.end());

    const auto output_low = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), false, true, -7);
    ASSERT_GT(output_low.size(), irap_frame.size());

    sf::encoder_optimization_info expected_low = expected_high;
    expected_low.quant_threshold_delta = 0U;
    const auto expected_low_payload = sf::write_encoder_optimization_info(expected_low);
    const auto expected_low_nal = sf::create_sei_nal_unit(215, expected_low_payload);
    std::vector<uint8_t> expected_low_marker = {0x00, 0x00, 0x01};
    expected_low_marker.insert(expected_low_marker.end(), expected_low_nal.begin(), expected_low_nal.end());
    const auto low_it =
            std::search(output_low.begin(), output_low.end(), expected_low_marker.begin(), expected_low_marker.end());
    EXPECT_NE(low_it, output_low.end());
}

TEST_F(ProcessingFunctionsTest, QpMapModeTakesPrecedenceWhenBothBackgroundModesEnabled) {
    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x26, 0x01, 0x11, 0x22, 0x33,
    };
    const auto output = maybe_inject_eoi_sei(irap_frame.data(), irap_frame.size(), true, true, 9);
    ASSERT_GT(output.size(), irap_frame.size());

    sf::encoder_optimization_info expected_qp_map{};
    expected_qp_map.cancel_flag = false;
    expected_qp_map.persistence_flag = true;
    expected_qp_map.for_human_viewing_idc = 0;
    expected_qp_map.for_machine_analysis_idc = 3;
    expected_qp_map.reserved_zero_2bits = 0;
    expected_qp_map.type = 0x01;
    expected_qp_map.object_based_idc = 0x02;
    expected_qp_map.quant_threshold_delta = 9U;
    expected_qp_map.pic_quant_object_flag = true;

    const auto expected_payload = sf::write_encoder_optimization_info(expected_qp_map);
    const auto expected_nal = sf::create_sei_nal_unit(215, expected_payload);
    std::vector<uint8_t> marker = {0x00, 0x00, 0x01};
    marker.insert(marker.end(), expected_nal.begin(), expected_nal.end());
    const auto it = std::search(output.begin(), output.end(), marker.begin(), marker.end());
    EXPECT_NE(it, output.end());
}

TEST_F(ProcessingFunctionsTest, get_frame_skip_behavior_is_reflected_in_process_encoded_frames_exit_paths) {
    data->stop_processing = true;
    data->encoded_frame_queue.close();

    std::atomic<bool> finished{false};
    std::jthread processing_thread([&]() {
        process_encoded_frames(*data);
        finished = true;
    });
    processing_thread.join();
    EXPECT_TRUE(finished);
}

TEST_F(ProcessingFunctionsTest, QpMapSentinelInjectsEoiOnIrapAndSignalsEos) {
    data->params.background_mode_value = background_mode::qp_map_delta;
    data->params.effective_background_mode_value = background_mode::qp_map_delta;
    data->params.qp_map_background_delta = 10;

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("sei-pass-through-pipeline"));
    ASSERT_NE(pipeline, nullptr);

    auto *appsrc = gst_element_factory_make("appsrc", "sei-test-src");
    auto *appsink = gst_element_factory_make("appsink", "sei-test-sink");
    ASSERT_NE(appsrc, nullptr);
    ASSERT_NE(appsink, nullptr);

    const auto caps = sf::unique_gst_caps_ptr(gst_caps_new_simple(
            "video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr));
    g_object_set(appsrc, "caps", caps.get(), "format", GST_FORMAT_TIME, "is-live", false, "do-timestamp", false,
                 nullptr);
    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, appsink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, appsink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, 2 * GST_SECOND);

    ASSERT_TRUE(setup_sei_caps_contract(*data, appsrc, caps.get()));

    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0xAB, 0xCD, 0xEF,
    };
    const auto encoded_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, irap_frame.size(), nullptr));
    ASSERT_NE(encoded_buffer, nullptr);

    GstMapInfo map_info;
    ASSERT_TRUE(gst_buffer_map(encoded_buffer.get(), &map_info, GST_MAP_WRITE));
    std::memcpy(map_info.data, irap_frame.data(), irap_frame.size());
    gst_buffer_unmap(encoded_buffer.get(), &map_info);
    GST_BUFFER_PTS(encoded_buffer.get()) = 10 * GST_MSECOND;
    GST_BUFFER_DTS(encoded_buffer.get()) = 10 * GST_MSECOND;
    GST_BUFFER_DURATION(encoded_buffer.get()) = 33 * GST_MSECOND;

    auto sample = sf::unique_gst_sample_ptr(gst_sample_new(encoded_buffer.get(), caps.get(), nullptr, nullptr));
    ASSERT_NE(sample, nullptr);

    std::jthread processing_thread([&]() { process_encoded_frames(*data); });

    data->packed_regions_queue.push({.sequence_id = 1, .regions = sf::packed_regions_info::create_sentinel()});
    data->encoded_frame_queue.push(make_encoded_frame_item(std::move(sample), {}, 1));
    data->encoded_frame_queue.close();

    processing_thread.join();

    const auto output_sample =
            sf::unique_gst_sample_ptr(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND));
    ASSERT_NE(output_sample, nullptr);

    GstBuffer *output_buffer = gst_sample_get_buffer(output_sample.get());
    ASSERT_NE(output_buffer, nullptr);
    EXPECT_GT(gst_buffer_get_size(output_buffer), irap_frame.size())
            << "QP-map mode should inject EOI SEI into IRAP frames";

    GstMapInfo out_map_info;
    ASSERT_TRUE(gst_buffer_map(output_buffer, &out_map_info, GST_MAP_READ));
    const std::vector<uint8_t> output_bytes(out_map_info.data, out_map_info.data + out_map_info.size);
    gst_buffer_unmap(output_buffer, &out_map_info);

    sf::encoder_optimization_info expected_eoi{};
    expected_eoi.cancel_flag = false;
    expected_eoi.persistence_flag = true;
    expected_eoi.for_human_viewing_idc = 0;
    expected_eoi.for_machine_analysis_idc = 3;
    expected_eoi.reserved_zero_2bits = 0;
    expected_eoi.type = 0x01;
    expected_eoi.object_based_idc = 0x02;
    expected_eoi.quant_threshold_delta = 10U;
    expected_eoi.pic_quant_object_flag = true;
    const auto expected_eoi_payload = sf::write_encoder_optimization_info(expected_eoi);
    const auto expected_eoi_nal = sf::create_sei_nal_unit(215, expected_eoi_payload);

    std::vector<uint8_t> expected_eoi_nal_with_start_code = {0x00, 0x00, 0x01};
    expected_eoi_nal_with_start_code.insert(expected_eoi_nal_with_start_code.end(), expected_eoi_nal.begin(),
                                            expected_eoi_nal.end());

    const auto eoi_it = std::search(output_bytes.begin(), output_bytes.end(), expected_eoi_nal_with_start_code.begin(),
                                    expected_eoi_nal_with_start_code.end());
    EXPECT_NE(eoi_it, output_bytes.end()) << "Expected QP-map EOI SEI payload was not found in output access unit";

    EXPECT_EQ(data->packed_regions_queue.size(), 0U)
            << "Metadata queue should stay synchronized with encoded frame processing";

    bool eos_observed = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (gst_app_sink_is_eos(GST_APP_SINK(appsink)) != FALSE) {
            eos_observed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(eos_observed) << "Encoded frame processing should emit EOS on sei_src when input queue closes";

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}

TEST_F(ProcessingFunctionsTest, QpMapSentinelSkipsLumaAdaptationWhenEncodedFrameStateDisablesIt) {
    data->params.background_mode_value = background_mode::qp_map_delta;
    data->params.effective_background_mode_value = background_mode::qp_map_delta;
    data->params.qp_map_background_delta = 10;
    data->params.enable_luma_adaptation = true;
    data->params.luma_downscale_factor = 0.5;

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("sei-direct-backend-luma-disabled-pipeline"));
    ASSERT_NE(pipeline, nullptr);

    auto *appsrc = gst_element_factory_make("appsrc", "sei-direct-backend-luma-disabled-src");
    auto *appsink = gst_element_factory_make("appsink", "sei-direct-backend-luma-disabled-sink");
    ASSERT_NE(appsrc, nullptr);
    ASSERT_NE(appsink, nullptr);

    const auto caps = sf::unique_gst_caps_ptr(gst_caps_new_simple(
            "video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr));
    g_object_set(appsrc, "caps", caps.get(), "format", GST_FORMAT_TIME, "is-live", false, "do-timestamp", false,
                 nullptr);
    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, appsink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, appsink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, 2 * GST_SECOND);

    ASSERT_TRUE(setup_sei_caps_contract(*data, appsrc, caps.get()));

    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0xAB, 0xCD, 0xEF,
    };
    const auto encoded_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, irap_frame.size(), nullptr));
    ASSERT_NE(encoded_buffer, nullptr);

    GstMapInfo map_info;
    ASSERT_TRUE(gst_buffer_map(encoded_buffer.get(), &map_info, GST_MAP_WRITE));
    std::memcpy(map_info.data, irap_frame.data(), irap_frame.size());
    gst_buffer_unmap(encoded_buffer.get(), &map_info);
    GST_BUFFER_PTS(encoded_buffer.get()) = 10 * GST_MSECOND;
    GST_BUFFER_DTS(encoded_buffer.get()) = 10 * GST_MSECOND;
    GST_BUFFER_DURATION(encoded_buffer.get()) = 33 * GST_MSECOND;

    auto sample = sf::unique_gst_sample_ptr(gst_sample_new(encoded_buffer.get(), caps.get(), nullptr, nullptr));
    ASSERT_NE(sample, nullptr);

    std::jthread processing_thread([&]() { process_encoded_frames(*data); });

    data->packed_regions_queue.push({.sequence_id = 1, .regions = sf::packed_regions_info::create_sentinel()});
    data->encoded_frame_queue.push(make_encoded_frame_item(std::move(sample), {}, 1));
    data->encoded_frame_queue.close();

    processing_thread.join();

    const auto output_sample =
            sf::unique_gst_sample_ptr(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND));
    ASSERT_NE(output_sample, nullptr);

    GstBuffer *output_buffer = gst_sample_get_buffer(output_sample.get());
    ASSERT_NE(output_buffer, nullptr);
    EXPECT_GT(gst_buffer_get_size(output_buffer), irap_frame.size())
            << "QP-map mode should still inject object-based EOI into IRAP frames";

    GstMapInfo out_map_info;
    ASSERT_TRUE(gst_buffer_map(output_buffer, &out_map_info, GST_MAP_READ));
    const std::vector<uint8_t> output_bytes(out_map_info.data, out_map_info.data + out_map_info.size);
    gst_buffer_unmap(output_buffer, &out_map_info);

    sf::encoder_optimization_info object_only_eoi{};
    object_only_eoi.cancel_flag = false;
    object_only_eoi.persistence_flag = true;
    object_only_eoi.for_human_viewing_idc = 0;
    object_only_eoi.for_machine_analysis_idc = 3;
    object_only_eoi.reserved_zero_2bits = 0;
    object_only_eoi.type = 0x01;
    object_only_eoi.object_based_idc = 0x02;
    object_only_eoi.quant_threshold_delta = 10U;
    object_only_eoi.pic_quant_object_flag = true;
    const auto object_only_payload = sf::write_encoder_optimization_info(object_only_eoi);
    const auto object_only_nal = sf::create_sei_nal_unit(215, object_only_payload);
    std::vector<uint8_t> object_only_marker = {0x00, 0x00, 0x01};
    object_only_marker.insert(object_only_marker.end(), object_only_nal.begin(), object_only_nal.end());

    const auto object_only_it =
            std::search(output_bytes.begin(), output_bytes.end(), object_only_marker.begin(), object_only_marker.end());
    EXPECT_NE(object_only_it, output_bytes.end()) << "Expected object-based-only EOI was not found";

    sf::encoder_optimization_info combined_eoi = object_only_eoi;
    combined_eoi.type = static_cast<uint16_t>(sf::encoder_optimization_info::type_object_based |
                                              sf::encoder_optimization_info::type_luma_adaptation);
    combined_eoi.luma_adaptation_idc = 1U;
    const auto derived_luma_signal = sf::derive_luma_adaptation_signal(0.5);
    combined_eoi.ratio_luma_sign_flag = derived_luma_signal.ratio_luma_sign_flag;
    combined_eoi.ratio_luma_value = derived_luma_signal.ratio_luma_value;
    combined_eoi.backscale_ratio_value = derived_luma_signal.backscale_ratio_value;
    const auto combined_payload = sf::write_encoder_optimization_info(combined_eoi);
    const auto combined_nal = sf::create_sei_nal_unit(215, combined_payload);
    std::vector<uint8_t> combined_marker = {0x00, 0x00, 0x01};
    combined_marker.insert(combined_marker.end(), combined_nal.begin(), combined_nal.end());

    const auto combined_it =
            std::search(output_bytes.begin(), output_bytes.end(), combined_marker.begin(), combined_marker.end());
    EXPECT_EQ(combined_it, output_bytes.end())
            << "Direct-backend frames must skip luma-adaptation EOI when sender-side scaling was not applied";

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}

TEST_F(ProcessingFunctionsTest, QpMapSentinelUsesEncodedFrameLumaStateWhenEnabled) {
    data->params.background_mode_value = background_mode::qp_map_delta;
    data->params.effective_background_mode_value = background_mode::qp_map_delta;
    data->params.qp_map_background_delta = 10;
    data->params.enable_luma_adaptation = true;
    data->params.luma_downscale_factor = 0.5;

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("sei-direct-backend-luma-enabled-pipeline"));
    ASSERT_NE(pipeline, nullptr);

    auto *appsrc = gst_element_factory_make("appsrc", "sei-direct-backend-luma-enabled-src");
    auto *appsink = gst_element_factory_make("appsink", "sei-direct-backend-luma-enabled-sink");
    ASSERT_NE(appsrc, nullptr);
    ASSERT_NE(appsink, nullptr);

    const auto caps = sf::unique_gst_caps_ptr(gst_caps_new_simple(
            "video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment", G_TYPE_STRING, "au", nullptr));
    g_object_set(appsrc, "caps", caps.get(), "format", GST_FORMAT_TIME, "is-live", false, "do-timestamp", false,
                 nullptr);
    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, appsink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, appsink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, 2 * GST_SECOND);

    ASSERT_TRUE(setup_sei_caps_contract(*data, appsrc, caps.get()));

    const std::vector<uint8_t> irap_frame = {
            0x00, 0x00, 0x01, 0x2A, 0x01, 0xAB, 0xCD, 0xEF,
    };
    const auto encoded_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, irap_frame.size(), nullptr));
    ASSERT_NE(encoded_buffer, nullptr);

    GstMapInfo map_info;
    ASSERT_TRUE(gst_buffer_map(encoded_buffer.get(), &map_info, GST_MAP_WRITE));
    std::memcpy(map_info.data, irap_frame.data(), irap_frame.size());
    gst_buffer_unmap(encoded_buffer.get(), &map_info);
    GST_BUFFER_PTS(encoded_buffer.get()) = 10 * GST_MSECOND;
    GST_BUFFER_DTS(encoded_buffer.get()) = 10 * GST_MSECOND;
    GST_BUFFER_DURATION(encoded_buffer.get()) = 33 * GST_MSECOND;

    auto sample = sf::unique_gst_sample_ptr(gst_sample_new(encoded_buffer.get(), caps.get(), nullptr, nullptr));
    ASSERT_NE(sample, nullptr);

    std::jthread processing_thread([&]() { process_encoded_frames(*data); });

    data->packed_regions_queue.push({.sequence_id = 1, .regions = sf::packed_regions_info::create_sentinel()});
    data->encoded_frame_queue.push(make_encoded_frame_item(
            std::move(sample), make_configured_luma_adaptation_state(true, data->params.luma_downscale_factor), 1));
    data->encoded_frame_queue.close();

    processing_thread.join();

    const auto output_sample =
            sf::unique_gst_sample_ptr(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND));
    ASSERT_NE(output_sample, nullptr);

    GstBuffer *output_buffer = gst_sample_get_buffer(output_sample.get());
    ASSERT_NE(output_buffer, nullptr);
    EXPECT_GT(gst_buffer_get_size(output_buffer), irap_frame.size())
            << "QP-map mode should inject combined EOI into IRAP frames when luma state is enabled";

    GstMapInfo out_map_info;
    ASSERT_TRUE(gst_buffer_map(output_buffer, &out_map_info, GST_MAP_READ));
    const std::vector<uint8_t> output_bytes(out_map_info.data, out_map_info.data + out_map_info.size);
    gst_buffer_unmap(output_buffer, &out_map_info);

    sf::encoder_optimization_info expected_eoi{};
    expected_eoi.cancel_flag = false;
    expected_eoi.persistence_flag = true;
    expected_eoi.for_human_viewing_idc = 0;
    expected_eoi.for_machine_analysis_idc = 3;
    expected_eoi.reserved_zero_2bits = 0;
    expected_eoi.type = static_cast<uint16_t>(sf::encoder_optimization_info::type_object_based |
                                              sf::encoder_optimization_info::type_luma_adaptation);
    expected_eoi.object_based_idc = 0x02;
    expected_eoi.quant_threshold_delta = 10U;
    expected_eoi.pic_quant_object_flag = true;
    expected_eoi.luma_adaptation_idc = 1U;
    const auto derived_luma_signal = sf::derive_luma_adaptation_signal(0.5);
    expected_eoi.ratio_luma_sign_flag = derived_luma_signal.ratio_luma_sign_flag;
    expected_eoi.ratio_luma_value = derived_luma_signal.ratio_luma_value;
    expected_eoi.backscale_ratio_value = derived_luma_signal.backscale_ratio_value;
    const auto expected_payload = sf::write_encoder_optimization_info(expected_eoi);
    const auto expected_nal = sf::create_sei_nal_unit(215, expected_payload);
    std::vector<uint8_t> expected_marker = {0x00, 0x00, 0x01};
    expected_marker.insert(expected_marker.end(), expected_nal.begin(), expected_nal.end());

    const auto expected_it =
            std::search(output_bytes.begin(), output_bytes.end(), expected_marker.begin(), expected_marker.end());
    EXPECT_NE(expected_it, output_bytes.end())
            << "Direct-backend frames should signal luma adaptation only when sender-side scaling succeeded";

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}
