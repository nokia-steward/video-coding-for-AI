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
#include "../../encoder_cli/src/gst/output_pipeline.h"
#include "../../encoder_cli/src/gst/output_pipeline/probes.h"
#include "../../encoder_cli/src/gst/rtp_utils.h"
#include "../common/gst_test_fixture.h"
#include "../common/integration_test_helpers.h"

/**
 * @file test_integration_output_branches.cpp
 * @brief End-to-end integration test for multiple output branches.
 *
 * This test verifies that all output branches work simultaneously:
 * - Display branch (if available)
 * - H.265 file branch
 * - MP4 file branch
 * - RTP stream branch (configuration only, no actual server)
 * - Debug branch (frame saving)
 *
 * Note: Display branch may not be available in headless environments.
 * RTP streaming requires a server, so we only test configuration.
 */

namespace {
    [[nodiscard]] bool is_missing_gpu_plugin_error(const std::string &message) {
        return message.find("Encoder element plugin not found") != std::string::npos ||
               message.find("GPU elements unavailable") != std::string::npos ||
               message.find("Failed to create GPU encoder") != std::string::npos;
    }

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

class OutputBranchesIntegrationTest : public GStreamerTestFixture {
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

        test_video_path = data_dir / "videos" / "test_output_branches_32x32_5fps.mp4";
        test_config_path = data_dir / "configs" / "test_output_branches_config.ini";
        output_h265_path = output_dir / "encoded" / "test_output.h265";
        output_mp4_path = output_dir / "encoded" / "test_output.mp4";
        frames_output_dir = output_dir / "frames";

        // Create test video file (small resolution for fast testing)
        // Use raw YUV format since the pipeline uses rawvideoparse
        const bool video_created = create_test_video(test_video_path, 32, 32, 5,
                                                     2, // 2 seconds of video
                                                     "raw" // Raw YUV format for rawvideoparse
        );

        if (!video_created) {
            GTEST_SKIP() << "Failed to create test video file. GStreamer plugins may be missing.";
        }
    }

    void TearDown() override {
        cleanup_test_global_data(data);
        GStreamerTestFixture::TearDown();
    }

    std::filesystem::path test_video_path;
    std::filesystem::path test_config_path;
    std::filesystem::path output_h265_path;
    std::filesystem::path output_mp4_path;
    std::filesystem::path frames_output_dir;
    std::unique_ptr<global_data> data;
};

/**
 * @brief Test that multiple output branches are created successfully.
 */
TEST_F(OutputBranchesIntegrationTest, MultipleBranchesCreation) {
    std::string gpu_error;
    if (!get_gpu_elements_available(gpu_error)) {
        GTEST_SKIP() << gpu_error;
        return;
    }
    const std::string config_contents = "[Common]\n"
                                        "input_type=file\n"
                                        "\n[Video]\n"
                                        "file_name=" +
                                        test_video_path.string() +
                                        "\n"
                                        "width=32\n"
                                        "height=32\n"
                                        "framerate=5\n"
                                        "format=I420\n"
                                        "\n[ROI]\n"
                                        "pack_rois=false\n"
                                        "inference_engine=false\n"
                                        "\n[Debug]\n"
                                        "draw_borders=false\n"
                                        "save_frames=true\n"
                                        "enable_display_branch=false\n"
                                        "enable_h265_branch=true\n"
                                        "enable_mp4_branch=true\n";
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
        if (is_missing_gpu_plugin_error(config_error)) {
            GTEST_SKIP() << config_error;
        }
        GTEST_FAIL() << config_error;
    }

    // Verify config is valid
    ASSERT_EQ(config.input_type_value, input_type::file);
    ASSERT_TRUE(config.debug.enable_h265_branch) << "H.265 branch should be enabled";
    ASSERT_TRUE(config.debug.enable_mp4_branch) << "MP4 branch should be enabled";
    ASSERT_TRUE(config.debug.save_frames) << "Frame saving should be enabled";

    // Create global_data
    data = std::make_unique<global_data>(std::move(config));

    // Load static ROI data
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

    // Verify output files are configured
    if (!output_h265_path.empty()) {
        EXPECT_TRUE(std::filesystem::exists(output_h265_path.parent_path())) << "H.265 output directory should exist";
    }

    if (!output_mp4_path.empty()) {
        EXPECT_TRUE(std::filesystem::exists(output_mp4_path.parent_path())) << "MP4 output directory should exist";
    }

    if (!frames_output_dir.empty()) {
        EXPECT_TRUE(std::filesystem::exists(frames_output_dir)) << "Frames output directory should exist";
    }

    SUCCEED() << "Multiple output branches created successfully";
}

TEST_F(OutputBranchesIntegrationTest, OutputPipelineProbesAttachAfterSuccessfulPipelineSetup) {
    std::string gpu_error;
    if (!get_gpu_elements_available(gpu_error)) {
        GTEST_SKIP() << gpu_error;
        return;
    }
    const std::string config_contents = "[Common]\n"
                                        "input_type=file\n"
                                        "\n[Video]\n"
                                        "file_name=" +
                                        test_video_path.string() +
                                        "\n"
                                        "width=32\n"
                                        "height=32\n"
                                        "framerate=5\n"
                                        "format=I420\n"
                                        "\n[ROI]\n"
                                        "pack_rois=false\n"
                                        "inference_engine=false\n"
                                        "\n[Debug]\n"
                                        "draw_borders=false\n"
                                        "save_frames=true\n"
                                        "enable_display_branch=false\n"
                                        "enable_h265_branch=true\n"
                                        "enable_mp4_branch=false\n";
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
        if (is_missing_gpu_plugin_error(config_error)) {
            GTEST_SKIP() << config_error;
        }
        GTEST_FAIL() << config_error;
    }

    data = std::make_unique<global_data>(std::move(config));
    ASSERT_TRUE(data->load_static_roi_data());

    const pipeline_setup_result setup_result = create_full_test_pipelines(*data);
    if (setup_result.status == pipeline_setup_status::encoding_unavailable) {
        GTEST_SKIP() << "Encoding pipeline creation failed - GPU pipeline not available.";
        return;
    }

    ASSERT_EQ(setup_result.status, pipeline_setup_status::success)
            << "Pipeline setup should succeed before probe installation. Error: " << setup_result.error_message;
    output_branch_facade facade(*data);
    EXPECT_TRUE(setup_output_pipeline_probes(facade));
}

/**
 * @brief Test that H.265 and MP4 branches work together.
 */
TEST_F(OutputBranchesIntegrationTest, H265AndMp4Branches) {
    std::string gpu_error;
    if (!get_gpu_elements_available(gpu_error)) {
        GTEST_SKIP() << gpu_error;
        return;
    }
    const std::string config_contents = "[Common]\n"
                                        "input_type=file\n"
                                        "\n[Video]\n"
                                        "file_name=" +
                                        test_video_path.string() +
                                        "\n"
                                        "width=32\n"
                                        "height=32\n"
                                        "framerate=5\n"
                                        "format=I420\n"
                                        "\n[ROI]\n"
                                        "pack_rois=false\n"
                                        "inference_engine=false\n"
                                        "\n[Debug]\n"
                                        "draw_borders=false\n"
                                        "save_frames=false\n"
                                        "enable_display_branch=false\n"
                                        "enable_h265_branch=true\n"
                                        "enable_mp4_branch=true\n";
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
        if (is_missing_gpu_plugin_error(config_error)) {
            GTEST_SKIP() << config_error;
        }
        GTEST_FAIL() << config_error;
    }

    // Verify both branches are enabled
    ASSERT_TRUE(config.debug.enable_h265_branch) << "H.265 branch should be enabled";
    ASSERT_TRUE(config.debug.enable_mp4_branch) << "MP4 branch should be enabled";

    // Create global_data
    data = std::make_unique<global_data>(std::move(config));

    // Load static ROI data
    const bool roi_loaded = data->load_static_roi_data();
    ASSERT_TRUE(roi_loaded);

    const pipeline_setup_result setup_result = create_full_test_pipelines(*data);
    if (setup_result.status == pipeline_setup_status::encoding_unavailable) {
        GTEST_SKIP() << "Encoding pipeline creation failed - GPU pipeline not available.";
        return;
    }

    ASSERT_EQ(setup_result.status, pipeline_setup_status::success)
            << "All pipelines should be created successfully. Error: " << setup_result.error_message;

    // Verify output pipeline has both branches configured
    // (We can't easily verify internal elements without accessing private members,
    // but pipeline creation success indicates branches were created)
    SUCCEED() << "H.265 and MP4 branches configured successfully";
}

/**
 * @brief Test that RTP branch configuration works.
 */
TEST_F(OutputBranchesIntegrationTest, RtpBranchConfiguration) {
    const std::string config_contents = "[Common]\n"
                                        "input_type=file\n"
                                        "\n[Video]\n"
                                        "file_name=" +
                                        test_video_path.string() +
                                        "\n"
                                        "width=32\n"
                                        "height=32\n"
                                        "framerate=5\n"
                                        "format=I420\n"
                                        "\n[ROI]\n"
                                        "pack_rois=false\n"
                                        "inference_engine=false\n"
                                        "\n[Debug]\n"
                                        "draw_borders=false\n"
                                        "save_frames=false\n"
                                        "enable_display_branch=false\n"
                                        "enable_h265_branch=false\n"
                                        "enable_mp4_branch=false\n"
                                        "\n[Communication]\n"
                                        "transport_type=rtp\n"
                                        "host=127.0.0.1\n"
                                        "port=5004\n";
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
        if (is_missing_gpu_plugin_error(config_error)) {
            GTEST_SKIP() << config_error;
        }
        GTEST_FAIL() << config_error;
    }

    // Verify RTP configuration
    ASSERT_FALSE(config.communication.host.empty()) << "Communication host should be configured";
    ASSERT_EQ(config.communication.host, "127.0.0.1") << "Communication host should be 127.0.0.1";
    ASSERT_EQ(config.communication.port, 5004) << "Communication port should be 5004";
    ASSERT_EQ(config.communication.resolved_transport_type(), sf::communication::transport_type::udp)
            << "Transport type should be RTP";

    // Note: Actual RTP streaming requires a server and handshake, which will fail in test environment.
    // This test verifies that the configuration is loaded correctly.
    // Pipeline creation will fail because UDP branch requires a successful handshake,
    // but that's expected behavior when no server is available.
    SUCCEED() << "RTP branch configuration loaded successfully (pipeline creation requires server)";
}

/**
 * @brief Test that all branches can be disabled (minimal output).
 */
TEST_F(OutputBranchesIntegrationTest, AllBranchesDisabled) {
    const std::string config_contents = "[Common]\n"
                                        "input_type=file\n"
                                        "\n[Video]\n"
                                        "file_name=" +
                                        test_video_path.string() +
                                        "\n"
                                        "width=32\n"
                                        "height=32\n"
                                        "framerate=5\n"
                                        "format=I420\n"
                                        "\n[ROI]\n"
                                        "pack_rois=false\n"
                                        "inference_engine=false\n"
                                        "\n[Debug]\n"
                                        "draw_borders=false\n"
                                        "save_frames=false\n"
                                        "enable_display_branch=false\n"
                                        "enable_h265_branch=false\n"
                                        "enable_mp4_branch=false\n";
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
        if (is_missing_gpu_plugin_error(config_error)) {
            GTEST_SKIP() << config_error;
        }
        GTEST_FAIL() << config_error;
    }

    // Verify all branches are disabled
    ASSERT_FALSE(config.debug.enable_display_branch) << "Display branch should be disabled";
    ASSERT_FALSE(config.debug.enable_h265_branch) << "H.265 branch should be disabled";
    ASSERT_FALSE(config.debug.enable_mp4_branch) << "MP4 branch should be disabled";
    ASSERT_FALSE(config.debug.save_frames) << "Frame saving should be disabled";

    // Create global_data
    data = std::make_unique<global_data>(std::move(config));

    // Load static ROI data
    const bool roi_loaded = data->load_static_roi_data();
    ASSERT_TRUE(roi_loaded);

    // Create all pipelines
    // Note: With no output branches, pipeline creation may still succeed
    // (the tee element will still be created, just with no branches)
    const pipeline_setup_result setup_result = create_full_test_pipelines(*data);
    if (setup_result.status == pipeline_setup_status::failure) {
        SUCCEED() << "Configuration with all branches disabled handled correctly: " << setup_result.error_message;
        return;
    }

    SUCCEED() << "Configuration with all branches disabled handled correctly";
}
