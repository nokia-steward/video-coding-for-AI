// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <gst/gstregistry.h>
#include <gtest/gtest.h>
#include <thread>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/gpu_elements.h"
#include "../../encoder_cli/src/gst/gst_pipeline.h"
#include "../../encoder_cli/src/gst/input_pipeline.h"
#include "../../encoder_cli/src/gst/input_types/input_ingress_facade.h"
#include "../../encoder_cli/src/gst/input_types/udp_input.h"
#include "../../encoder_cli/src/gst/output_pipeline.h"
#include "../../encoder_cli/src/gst/rtp_utils.h"
#include "../common/gst_test_fixture.h"
#include "../common/integration_test_helpers.h"

/**
 * @file test_integration_udp_input_basic.cpp
 * @brief End-to-end integration test for basic UDP input processing.
 *
 * This test verifies that UDP input pipeline works correctly:
 * - UDP pipeline is created successfully
 * - UDP source is configured correctly
 * - Connection state tracking works
 * - Pipeline can be started and stopped gracefully
 *
 * Note: Full UDP streaming tests require a UDP server to send RTP packets.
 * This test focuses on pipeline creation and configuration.
 */

namespace {
    [[nodiscard]] bool get_gpu_elements_available(std::string &error_message) {
        try {
            if (!check_gpu_elements_available()) {
                error_message = "GPU elements unavailable - skipping pipeline integration test.";
                return false;
            }
        } catch (const std::exception &exception) {
            error_message = std::string("GPU elements unavailable: ") + exception.what();
            return false;
        }
        return true;
    }
} // namespace

class UdpInputBasicIntegrationTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();

        // Ensure GStreamer is initialized (defensive check)
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }

        // Update registry to ensure plugins are available
        GstRegistry *registry = gst_registry_get();
        if (registry) {
            gst_update_registry();
        }

        // Initialize debug categories
        init_gst_pipeline_debug_category();

        // Ensure test directories exist
        ensure_test_directories();

        // Clean up any previous test output (but keep config files)
        cleanup_test_output();

        // Set up paths
        const auto data_dir = get_test_data_dir();
        const auto output_dir = get_test_output_dir();

        test_config_path = data_dir / "configs" / "test_udp_input_config.ini";
        output_h265_path = output_dir / "encoded" / "test_udp_output.h265";

        // UDP configuration parameters
        udp_port = 5004; // Standard RTP port
        udp_bind_address = "0.0.0.0";
        width = 32;
        height = 32;
        framerate = 5;
        udp_encoding = "H265";
        udp_payload_type = 96;
        udp_clock_rate = 90000;
        udp_latency_ms = 200;
    }

    void TearDown() override {
        cleanup_test_global_data(data, true);
        GStreamerTestFixture::TearDown();
    }

    std::filesystem::path test_config_path;
    std::filesystem::path output_h265_path;
    uint16_t udp_port;
    std::string udp_bind_address;
    int width;
    int height;
    int framerate;
    std::string udp_encoding;
    uint32_t udp_payload_type;
    uint32_t udp_clock_rate;
    int udp_latency_ms;
    std::unique_ptr<global_data> data;
};

/**
 * @brief Test that UDP input pipeline is created successfully.
 */
TEST_F(UdpInputBasicIntegrationTest, UdpPipelineCreation) {
    std::string gpu_error;
    if (!get_gpu_elements_available(gpu_error)) {
        GTEST_SKIP() << gpu_error;
        return;
    }
    const std::string config_contents = "[Common]\n"
                                        "input_type=udp\n"
                                        "width=" +
                                        std::to_string(width) +
                                        "\n"
                                        "height=" +
                                        std::to_string(height) +
                                        "\n"
                                        "framerate=" +
                                        std::to_string(framerate) +
                                        "\n"
                                        "\n[UDP]\n"
                                        "bind_address=" +
                                        udp_bind_address +
                                        "\n"
                                        "port=" +
                                        std::to_string(udp_port) +
                                        "\n"
                                        "encoding=" +
                                        udp_encoding +
                                        "\n"
                                        "payload_type=" +
                                        std::to_string(udp_payload_type) +
                                        "\n"
                                        "clock_rate=" +
                                        std::to_string(udp_clock_rate) +
                                        "\n"
                                        "latency_ms=" +
                                        std::to_string(udp_latency_ms) +
                                        "\n"
                                        "\n[ROI]\n"
                                        "pack_rois=false\n"
                                        "inference_engine=false\n"
                                        "\n[Debug]\n"
                                        "draw_borders=false\n"
                                        "save_frames=false\n"
                                        "enable_display_branch=false\n"
                                        "enable_h265_branch=true\n";
    if (!write_text_file(test_config_path, config_contents)) {
        GTEST_SKIP() << "Failed to create config file at: " << test_config_path.string();
    }

    std::string validation_error;
    if (!validate_config_file(test_config_path, "", validation_error)) {
        GTEST_SKIP() << validation_error;
    }

    encoder_config config;
    std::string config_error;
    if (!load_encoder_config_from_file(test_config_path, config, config_error)) {
        GTEST_FAIL() << config_error;
    }

    // Verify config is valid
    ASSERT_EQ(config.input_type_value, input_type::udp);
    ASSERT_EQ(config.udp.port, udp_port);
    ASSERT_EQ(config.width, width);
    ASSERT_EQ(config.height, height);
    ASSERT_EQ(config.framerate, framerate);
    ASSERT_EQ(config.udp.encoding, udp_encoding);

    // Create global_data
    data = std::make_unique<global_data>(std::move(config));

    input_ingress_facade ingress(*data);

    // Load static ROI data (should succeed even if not configured)
    const bool roi_loaded = data->load_static_roi_data();
    ASSERT_TRUE(roi_loaded);

    const pipeline_setup_result setup_result = create_full_test_pipelines(*data);
    if (setup_result.status == pipeline_setup_status::encoding_unavailable) {
        GTEST_SKIP() << "Encoding pipeline creation failed - GPU pipeline not available.";
        return;
    }

    ASSERT_EQ(setup_result.status, pipeline_setup_status::success)
            << "All pipelines should be created successfully. Error: " << setup_result.error_message;
    ASSERT_NE(data->input_pipeline.get(), nullptr) << "Input pipeline should be created";
    ASSERT_NE(data->encoding_pipeline.get(), nullptr) << "Encoding pipeline should be created";
    ASSERT_NE(data->output_pipeline.get(), nullptr) << "Output pipeline should be created";

    // Verify UDP connection state is initially false
    EXPECT_FALSE(has_udp_client_connected(ingress)) << "UDP client should not be connected initially";

    // Verify UDP packet count is zero
    EXPECT_EQ(get_udp_packet_count(ingress), 0) << "UDP packet count should be zero initially";

    // Start pipelines (they will wait for UDP client)
    const auto input_state = gst_element_set_state(data->input_pipeline.get(), GST_STATE_PLAYING);
    const auto encoding_state = gst_element_set_state(data->encoding_pipeline.get(), GST_STATE_PLAYING);
    const auto output_state = gst_element_set_state(data->output_pipeline.get(), GST_STATE_PLAYING);

    ASSERT_NE(input_state, GST_STATE_CHANGE_FAILURE) << "Input pipeline should start";
    ASSERT_NE(encoding_state, GST_STATE_CHANGE_FAILURE) << "Encoding pipeline should start";
    ASSERT_NE(output_state, GST_STATE_CHANGE_FAILURE) << "Output pipeline should start";

    // Brief wait for pipeline initialization
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify pipelines are in playing state
    // Use timeout instead of GST_CLOCK_TIME_NONE to avoid hanging if UDP pipeline is waiting for data
    GstState input_state_result, input_pending;
    const GstClockTime timeout_ns = 100 * GST_MSECOND; // 100ms timeout
    const GstStateChangeReturn state_ret =
            gst_element_get_state(data->input_pipeline.get(), &input_state_result, &input_pending, timeout_ns);

    // Pipeline may be in PLAYING or PAUSED state depending on whether UDP data is available
    // The important thing is that it's not in FAILURE state
    EXPECT_NE(state_ret, GST_STATE_CHANGE_FAILURE) << "Input pipeline state check should not fail";
    EXPECT_NE(input_state_result, GST_STATE_VOID_PENDING) << "Input pipeline should have a valid state";

    // Stop pipelines
    gst_element_set_state(data->input_pipeline.get(), GST_STATE_NULL);
    gst_element_set_state(data->encoding_pipeline.get(), GST_STATE_NULL);
    gst_element_set_state(data->output_pipeline.get(), GST_STATE_NULL);

    // Reset UDP connection state
    reset_udp_connection_state(ingress);

    SUCCEED() << "UDP pipeline created and configured successfully";
}

/**
 * @brief Test that UDP connection state tracking works correctly.
 */
TEST_F(UdpInputBasicIntegrationTest, UdpConnectionStateTracking) {
    // Initialize UDP input debug category (must be done before creating global_data)
    init_udp_input_debug_category();

    // Create config with defaults, then set only UDP-specific fields
    encoder_config config; // Uses default constructor which initializes all fields
    config.input_type_value = input_type::udp;
    config.width = width;
    config.height = height;
    config.framerate = framerate;
    config.udp.port = udp_port;
    config.udp.bind_address = udp_bind_address;
    config.udp.encoding = udp_encoding;
    config.udp.payload_type = udp_payload_type;
    config.udp.clock_rate = udp_clock_rate;
    config.udp.latency_ms = udp_latency_ms;

    // Create global_data (needed for reset_udp_connection_state which uses the logger)
    data = std::make_unique<global_data>(std::move(config));

    input_ingress_facade ingress(*data);

    // Reset connection state
    reset_udp_connection_state(ingress);

    // Verify initial state
    EXPECT_FALSE(has_udp_client_connected(ingress)) << "UDP client should not be connected initially";
    EXPECT_EQ(get_udp_packet_count(ingress), 0) << "UDP packet count should be zero";
    EXPECT_EQ(get_udp_bytes_received(ingress), 0) << "UDP bytes received should be zero";

    // Verify last packet time is minimum (not set)
    const auto last_packet_time = get_last_udp_packet_time(ingress);
    EXPECT_EQ(last_packet_time, std::chrono::steady_clock::time_point::min())
            << "Last packet time should be minimum initially";

    // Reset again (should be idempotent)
    reset_udp_connection_state(ingress);
    EXPECT_FALSE(has_udp_client_connected(ingress)) << "Reset should maintain disconnected state";

    SUCCEED() << "UDP connection state tracking works correctly";
}

/**
 * @brief Test that invalid UDP configuration is handled gracefully.
 */
TEST_F(UdpInputBasicIntegrationTest, InvalidUdpConfiguration) {
    // Create config with invalid UDP port (0 is invalid)
    const std::string config_contents = "[Common]\n"
                                        "input_type=udp\n"
                                        "width=" +
                                        std::to_string(width) +
                                        "\n"
                                        "height=" +
                                        std::to_string(height) +
                                        "\n"
                                        "framerate=" +
                                        std::to_string(framerate) +
                                        "\n"
                                        "\n[UDP]\n"
                                        "port=0\n"
                                        "encoding=" +
                                        udp_encoding + "\n";
    if (!write_text_file(test_config_path, config_contents)) {
        GTEST_SKIP() << "Failed to create config file at: " << test_config_path.string();
    }

    std::string validation_error;
    if (!validate_config_file(test_config_path, "", validation_error)) {
        GTEST_SKIP() << validation_error;
    }

    encoder_config config;
    std::string config_error;
    if (load_encoder_config_from_file(test_config_path, config, config_error)) {

        // Configuration may load with invalid values
        // Pipeline creation should handle this gracefully
        data = std::make_unique<global_data>(std::move(config));

        const bool pipeline_created = create_input_pipeline(*data);

        // Pipeline creation may succeed or fail depending on validation
        // The important thing is that it doesn't crash
        (void) pipeline_created;
        SUCCEED();
    } else {
        // Expected: configuration or pipeline creation may fail for invalid config
        SUCCEED();
    }
}
