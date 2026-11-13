// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <gst/gstregistry.h>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

#include "../../encoder_cli/src/cli_params.h"
#include "../../encoder_cli/src/configuration_manager.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/gst_pipeline.h"
#include "../../encoder_cli/src/gst/input_pipeline.h"
#include "../../encoder_cli/src/gst/output_pipeline.h"
#include "../../encoder_cli/src/gst/rtp_utils.h"
#include "../../encoder_cli/src/processing/encoded_frame_processing.h"
#include "../../encoder_cli/src/processing/pipeline_message_processing.h"
#include "../../encoder_cli/src/processing/roi_detection_processing.h"
#include "../../encoder_cli/src/roi/low_delay_packer.h"
#include "../common/gst_test_fixture.h"
#include "../common/integration_test_helpers.h"

/**
 * @file test_integration_low_delay_packer.cpp
 * @brief End-to-end integration test for low_delay packer position maintenance.
 *
 * This test verifies that low_delay packer maintains ROI positions correctly:
 * - ROI positions remain stable when sizes don't change
 * - Positions adjust correctly when ROIs grow/shrink
 * - Weight-based priority is respected
 * - ROI accumulation and merging work correctly
 *
 * Note: This test uses static ROI data to verify low_delay packing behavior
 * across multiple frames. For frame-by-frame ROI changes, ROI data files
 * would be needed.
 */

class LowDelayPackerIntegrationTest : public GStreamerTestFixture {
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

        test_video_path = data_dir / "videos" / "test_low_delay_packer_640x480_5fps.mp4";
        test_config_path = data_dir / "configs" / "test_low_delay_packer_config.ini";
        static_roi_json_path = data_dir / "roi_data" / "test_low_delay_packer_rois.json";
        output_h265_path = output_dir / "encoded" / "test_low_delay_packer_output.h265";

        // Create test video file (640x480 to fit multiple CTU-aligned ROIs)
        // Use raw YUV format since the pipeline uses rawvideoparse
        const bool video_created = create_test_video(test_video_path, 640, 480, 5,
                                                     3, // 3 seconds of video for multiple frames
                                                     "raw" // Raw YUV format for rawvideoparse
        );

        if (!video_created) {
            GTEST_SKIP() << "Failed to create test video file. GStreamer plugins may be missing.";
        }

        // Create static ROI JSON file with multiple ROIs of different sizes (scaled for 640x480)
        // This tests weight-based priority (larger ROIs should have priority)
        // Each ROI must be placeable within the CTU grid (positions/sizes reasonable for 640x480)
        const std::vector<std::tuple<int, int, int, int>> test_rois = {
                {50, 50, 150, 120}, // Large ROI - high priority
                {250, 80, 80, 60}, // Medium ROI - medium priority
                {400, 200, 60, 40}, // Small ROI - low priority
                {80, 250, 100, 80}, // Medium ROI - medium priority
        };

        const bool roi_json_created = create_static_roi_json(static_roi_json_path, test_rois);

        if (!roi_json_created) {
            GTEST_SKIP() << "Failed to create static ROI JSON file.";
        }

        // Create test config file with low_delay packing enabled
        std::ofstream config_file(test_config_path);
        if (config_file.is_open()) {
            config_file << "[Common]\n";
            config_file << "input_type=file\n";

            config_file << "\n[Video]\n";
            config_file << "file_name=" << test_video_path.string() << "\n";
            config_file << "width=640\n";
            config_file << "height=480\n";
            config_file << "framerate=5\n";
            config_file << "format=I420\n";

            config_file << "\n[ROI]\n";
            config_file << "static_roi_json_file=" << static_roi_json_path.string() << "\n";
            config_file << "pack_rois=true\n"; // Enable packing to use low_delay packer
            config_file << "packer_type=low_delay\n"; // Use low_delay packer
            config_file << "num_frames=3\n"; // Number of frames to accumulate
            config_file << "inference_engine=false\n";

            config_file << "\n[Debug]\n";
            config_file << "draw_borders=false\n";
            config_file << "save_frames=false\n";
            config_file << "enable_display_branch=false\n";
            config_file << "enable_h265_branch=true\n";

            config_file.flush();
            config_file.close();
        }

        if (!std::filesystem::exists(test_config_path)) {
            GTEST_SKIP() << "Failed to create test config file.";
        }

        // Verify file has content
        const auto file_size = std::filesystem::file_size(test_config_path);
        if (file_size == 0) {
            GTEST_SKIP() << "Config file was created but is empty.";
        }

        // Verify file_name is in the file (basic sanity check)
        std::ifstream verify_file(test_config_path);
        if (verify_file.is_open()) {
            std::string line;
            bool found_file_name = false;
            while (std::getline(verify_file, line)) {
                if (line.find("file_name=") != std::string::npos) {
                    found_file_name = true;
                    break;
                }
            }
            verify_file.close();
            if (!found_file_name) {
                GTEST_SKIP() << "Config file missing file_name field.";
            }
        }
    }

    void TearDown() override {
        // Clean up pipelines
        if (data) {
            data->stop_processing = true;
            data->raw_frame_queue.close();
            data->encoded_frame_queue.close();
            data->packed_regions_queue.close();

            // Give threads time to finish
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            data.reset();
        }

        GStreamerTestFixture::TearDown();
    }

    std::filesystem::path test_video_path;
    std::filesystem::path test_config_path;
    std::filesystem::path static_roi_json_path;
    std::filesystem::path output_h265_path;
    std::unique_ptr<global_data> data;
};
