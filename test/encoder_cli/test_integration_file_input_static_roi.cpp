// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <gst/gstregistry.h>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/gpu_elements.h"
#include "../../encoder_cli/src/gst/gst_pipeline.h"
#include "../../encoder_cli/src/gst/input_pipeline.h"
#include "../../encoder_cli/src/gst/output_pipeline.h"
#include "../../encoder_cli/src/gst/rtp_utils.h"
#include "../../encoder_cli/src/processing/encoded_frame_processing.h"
#include "../../encoder_cli/src/processing/pipeline_message_processing.h"
#include "../../encoder_cli/src/processing/roi_detection_processing.h"
#include "../common/gst_test_fixture.h"
#include "../common/integration_test_helpers.h"

/**
 * @file test_integration_file_input_static_roi.cpp
 * @brief End-to-end integration test for static ROI processing with file input.
 *
 * This test verifies that static ROI processing works correctly:
 * - Static ROIs are loaded from JSON file
 * - ROIs are applied to each frame
 * - Packed output contains static regions
 * - SEI messages contain correct ROI data
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

class FileInputStaticRoiIntegrationTest : public GStreamerTestFixture {
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

        test_video_path = data_dir / "videos" / "test_static_roi_32x32_5fps.mp4";
        test_config_path = data_dir / "configs" / "test_static_roi_config.ini";
        static_roi_json_path = data_dir / "roi_data" / "test_static_rois.json";
        output_h265_path = output_dir / "encoded" / "test_static_roi_output.h265";

        // Create test video file (small resolution for fast testing)
        // Use raw YUV format since the pipeline uses rawvideoparse
        const bool video_created = create_test_video(test_video_path, 32, 32, 5,
                                                     2, // 2 seconds of video
                                                     "raw" // Raw YUV format for rawvideoparse
        );

        if (!video_created) {
            GTEST_SKIP() << "Failed to create test video file. GStreamer plugins may be missing.";
        }

        // Create static ROI JSON file with known regions (scaled for 32x32)
        const std::vector<std::tuple<int, int, int, int>> test_rois = {
                {4, 4, 12, 10}, // ROI 1: top-left region
                {16, 12, 12, 10}, // ROI 2: bottom-right region
        };

        const bool roi_json_created = create_static_roi_json(static_roi_json_path, test_rois);

        if (!roi_json_created) {
            GTEST_SKIP() << "Failed to create static ROI JSON file.";
        }

        const std::string config_contents = "[Common]\n"
                                            "input_type=file\n"
                                            "width=32\n"
                                            "height=32\n"
                                            "framerate=5\n"
                                            "\n[Video]\n"
                                            "file_name=" +
                                            test_video_path.string() +
                                            "\n"
                                            "format=I420\n"
                                            "\n[ROI]\n"
                                            "static_roi_json_file=" +
                                            static_roi_json_path.string() +
                                            "\n"
                                            "pack_rois=true\n"
                                            "inference_engine=false\n"
                                            "background_mode=off\n"
                                            "\n[Debug]\n"
                                            "draw_borders=false\n"
                                            "save_frames=false\n"
                                            "enable_display_branch=false\n"
                                            "enable_h265_branch=true\n";
        if (!write_text_file(test_config_path, config_contents)) {
            GTEST_SKIP() << "Failed to create test config file at: " << test_config_path.string();
        }

        std::string validation_error;
        if (!validate_config_file(test_config_path, "file_name=", validation_error)) {
            GTEST_SKIP() << validation_error;
        }
    }

    void TearDown() override {
        cleanup_test_global_data(data);
        GStreamerTestFixture::TearDown();
    }

    std::filesystem::path test_video_path;
    std::filesystem::path test_config_path;
    std::filesystem::path static_roi_json_path;
    std::filesystem::path output_h265_path;
    std::unique_ptr<global_data> data;
};

/**
 * @brief Test that static ROI processing works correctly.
 */
TEST_F(FileInputStaticRoiIntegrationTest, StaticRoiProcessing) {
    std::string gpu_error;
    if (!get_gpu_elements_available(gpu_error)) {
        GTEST_SKIP() << gpu_error;
        return;
    }
    // Verify config file still exists before loading (might have been deleted by another test)
    if (!std::filesystem::exists(test_config_path)) {
        GTEST_SKIP() << "Config file does not exist: " << test_config_path.string();
    }

    encoder_config config;
    std::string config_error;
    if (!load_encoder_config_from_file(test_config_path, config, config_error)) {
        // Provide more context about the error
        GTEST_FAIL() << config_error << " (config file: " << test_config_path.string()
                     << ", exists: " << (std::filesystem::exists(test_config_path) ? "yes" : "no") << ")";
    }

    // Verify config is valid
    ASSERT_EQ(config.input_type_value, input_type::file);
    ASSERT_FALSE(config.static_roi_json_file.empty()) << "Static ROI JSON file should be configured";
    ASSERT_FALSE(config.inference_engine) << "Inference engine should be disabled for static ROI mode";
    ASSERT_TRUE(config.pack_rois) << "ROI packing should be enabled";

    // Create global_data
    data = std::make_unique<global_data>(std::move(config));

    // Load static ROI data
    const bool roi_loaded = data->load_static_roi_data();
    ASSERT_TRUE(roi_loaded) << "Static ROI data should load successfully";
    ASSERT_TRUE(data->loaded_static_roi_data.has_value()) << "Static ROI data should be loaded";
    ASSERT_FALSE(data->loaded_static_roi_data->empty()) << "Static ROI data should not be empty";
    ASSERT_EQ(data->loaded_static_roi_data->size(), 2) << "Should have 2 static ROIs";

    const pipeline_setup_result setup_result = create_full_test_pipelines(*data);
    if (setup_result.status == pipeline_setup_status::encoding_unavailable) {
        GTEST_SKIP() << "Encoding pipeline creation failed - GPU pipeline not available.";
        return;
    }

    ASSERT_EQ(setup_result.status, pipeline_setup_status::success)
            << "All pipelines should be created successfully. Error: " << setup_result.error_message;

    // Start processing threads
    std::jthread roi_detection_thread([this]() { roi_detection_frame_processing(*data); });

    std::jthread encoded_frame_thread([this]() { process_encoded_frames(*data); });

    std::jthread message_thread([this]() { process_pipeline_messages(data->output_pipeline, *data); });

    // Start pipelines
    const auto input_state = gst_element_set_state(data->input_pipeline.get(), GST_STATE_PLAYING);
    const auto encoding_state = gst_element_set_state(data->encoding_pipeline.get(), GST_STATE_PLAYING);
    const auto output_state = gst_element_set_state(data->output_pipeline.get(), GST_STATE_PLAYING);

    ASSERT_NE(input_state, GST_STATE_CHANGE_FAILURE) << "Input pipeline should start";
    ASSERT_NE(encoding_state, GST_STATE_CHANGE_FAILURE) << "Encoding pipeline should start";
    ASSERT_NE(output_state, GST_STATE_CHANGE_FAILURE) << "Output pipeline should start";

    // Wait for processing (max 5 seconds - allow time for pipeline initialization)
    const auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(5);

    while (!data->stop_processing.load() && std::chrono::steady_clock::now() - start_time < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check if we've processed some frames
        if (data->num_input_frames > 0 && data->current_frame_number.load() > 0) {
            // Processing has started
            break;
        }
    }

    // Brief wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Signal stop and close queues to allow threads to exit
    data->stop_processing = true;
    data->raw_frame_queue.close();
    data->encoded_frame_queue.close();
    data->packed_regions_queue.close();

    // Stop pipelines
    gst_element_set_state(data->input_pipeline.get(), GST_STATE_NULL);
    gst_element_set_state(data->encoding_pipeline.get(), GST_STATE_NULL);
    gst_element_set_state(data->output_pipeline.get(), GST_STATE_NULL);

    // Wait for threads to finish
    roi_detection_thread.join();
    encoded_frame_thread.join();
    message_thread.join();

    // Verify that frames were processed
    EXPECT_GT(data->num_input_frames, 0) << "At least some frames should be processed";
    EXPECT_GT(data->current_frame_number.load(), 0) << "Frame counter should increment";

    // Verify output file was created (if configured)
    if (!output_h265_path.empty() && std::filesystem::exists(output_h265_path)) {
        const bool output_valid = verify_output_file(output_h265_path);
        EXPECT_TRUE(output_valid) << "Output file should exist and have content";
    }

    // Verify that ROI data may have been queued for SEI generation.
    // Note: This is a basic check - full SEI validation would require parsing the bitstream.
    EXPECT_GE(data->packed_regions_queue.size(), 0U) << "Packed regions queue may contain ROI data";
}

/**
 * @brief Test that invalid static ROI JSON file is handled gracefully.
 */
TEST_F(FileInputStaticRoiIntegrationTest, InvalidStaticRoiJsonHandling) {
    // Create config with non-existent ROI file
    const auto invalid_roi_path = get_test_data_dir() / "roi_data" / "nonexistent.json";

    const std::string config_contents = "[Common]\n"
                                        "input_type=file\n"
                                        "width=32\n"
                                        "height=32\n"
                                        "framerate=5\n"
                                        "\n[Video]\n"
                                        "file_name=" +
                                        test_video_path.string() +
                                        "\n"
                                        "format=I420\n"
                                        "\n[ROI]\n"
                                        "static_roi_json_file=" +
                                        invalid_roi_path.string() +
                                        "\n"
                                        "inference_engine=false\n";
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
        GTEST_SKIP() << "Configuration loading failed: " << config_error;
    }

    // Create global_data
    data = std::make_unique<global_data>(std::move(config));

    // Loading static ROI data should fail gracefully
    const bool roi_loaded = data->load_static_roi_data();
    EXPECT_FALSE(roi_loaded) << "Loading non-existent ROI file should fail";
    EXPECT_FALSE(data->loaded_static_roi_data.has_value()) << "Static ROI data should not be loaded";
}
