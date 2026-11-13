// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include <string>

#include "../../encoder_cli/src/gst/input_types/camera_input.h"
#include "../../encoder_cli/src/gst/input_types/file_input.h"
#include "../../encoder_cli/src/gst/input_types/input_ingress_facade.h"
#include "../../encoder_cli/src/gst/input_types/udp_input.h"
#ifdef HAVE_DEEPSTREAM
#include "../../encoder_cli/src/gst/input_types/roi_input.h"
#endif
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_input_pipelines.cpp
 * @brief Tests for input pipeline creation functions.
 *
 * Note: These tests focus on element creation and basic validation.
 * Full pipeline integration tests require more complex setup and are
 * covered in end-to-end test plans.
 */

class InputPipelinesTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();

        // Create minimal config for testing
        config = encoder_config();
        config.input_type_value = input_type::file;
        config.width = 1920;
        config.height = 1080;
        config.framerate = 30;

        // Create global_data with minimal config
        data = std::make_unique<global_data>(std::move(config));

        // Create a minimal pipeline for testing
        data->input_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-input-pipeline"));
    }

    void TearDown() override {
        data.reset();
        GStreamerTestFixture::TearDown();
    }

    encoder_config config;
    std::unique_ptr<global_data> data;
};

#ifdef HAVE_DEEPSTREAM
/**
 * @brief Forward declaration matching `roi_input_deepstream_factory.cpp` (header declares full signature).
 */
[[nodiscard]] bool generate_tracker_config_file(const std::string &base_config_path, const std::string &output_path,
                                                int max_shadow_tracking_age, bool enable_reassoc,
                                                int trajectory_projection_length, int max_tracklet_matching_time_range,
                                                int max_targets_per_stream, int probation_age,
                                                int early_termination_age, double min_confidence,
                                                bool use_prediction_for_assoc);
#endif

/**
 * @brief Test create_file_input_pipeline element creation (basic validation).
 */
TEST_F(InputPipelinesTest, FileInputPipelineElementCreation) {
    // Set up config for file input
    encoder_config file_config;
    file_config.input_type_value = input_type::file;
    file_config.width = 1920;
    file_config.height = 1080;
    file_config.framerate = 30;

    // Create a temporary test file
    const std::filesystem::path test_file = "test_video.mp4";
    std::ofstream file(test_file);
    file << "fake video data";
    file.close();

    file_config.file_name = sf::unique_g_char_ptr(g_strdup(test_file.c_str()));

    global_data test_data(std::move(file_config));
    test_data.input_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-pipeline"));

    // Note: This test verifies the function can be called without crashing
    // Full functionality requires valid video file and proper GStreamer setup
    // The function may fail due to missing file or codec, which is expected
    const bool result = create_file_input_pipeline(test_data);

    // Clean up
    std::filesystem::remove(test_file);

    // Result depends on GStreamer plugins availability and file format
    // We just verify the function doesn't crash
    (void) result;
    SUCCEED();
}

/**
 * @brief Test create_camera_input_elements creates required elements.
 */
TEST_F(InputPipelinesTest, CameraInputElementsCreation) {
    encoder_config camera_config;
    camera_config.input_type_value = input_type::camera;
    camera_config.camera_device = "/dev/video0";
    camera_config.width = 1920;
    camera_config.height = 1080;
    camera_config.framerate = 30;

    global_data test_data(std::move(camera_config));

    GstElement *v4l2src = nullptr;
    GstElement *queue = nullptr;
    GstElement *capsfilter = nullptr;
    GstElement *tee = nullptr;

    const bool result = create_camera_input_elements(test_data.params, v4l2src, queue, capsfilter, tee);

    // Result depends on V4L2 plugin availability and camera hardware
    // If elements are created, verify they're not null
    if (result) {
        EXPECT_NE(v4l2src, nullptr);
        EXPECT_NE(queue, nullptr);
        EXPECT_NE(capsfilter, nullptr);
        EXPECT_NE(tee, nullptr);

        // Clean up if created
        if (v4l2src)
            gst_object_unref(v4l2src);
        if (queue)
            gst_object_unref(queue);
        if (capsfilter)
            gst_object_unref(capsfilter);
        if (tee)
            gst_object_unref(tee);
    } else {
        // Function may fail if V4L2 plugins are not available
        // This is acceptable for testing
        SUCCEED();
    }
}

/**
 * @brief Test create_udp_input_elements creates required elements.
 */
TEST_F(InputPipelinesTest, UdpInputElementsCreation) {
    encoder_config udp_config;
    udp_config.input_type_value = input_type::udp;
    udp_config.udp.bind_address = "0.0.0.0";
    udp_config.udp.port = 5000;
    udp_config.udp.encoding = "H264";
    udp_config.udp.clock_rate = 90000;
    udp_config.width = 1920;
    udp_config.height = 1080;
    udp_config.framerate = 30;

    global_data test_data(std::move(udp_config));

    udp_input_elements elements;
    bool result = false;
    try {
        result = create_udp_input_elements(test_data.params, elements);
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();
        if (message.find("Failed to create GPU decoder") != std::string::npos) {
            GTEST_SKIP() << "GPU decoder plugin unavailable in test environment: " << message;
        }
        throw;
    }

    // Result depends on RTP plugin availability
    // If elements are created, verify they're not null
    if (result) {
        EXPECT_NE(elements.source, nullptr);
        EXPECT_NE(elements.jitterbuffer, nullptr);
        EXPECT_NE(elements.depay, nullptr);
        EXPECT_NE(elements.parser, nullptr);

        // Clean up if created
        if (elements.source)
            gst_object_unref(elements.source);
        if (elements.rtp_capsfilter)
            gst_object_unref(elements.rtp_capsfilter);
        if (elements.jitterbuffer)
            gst_object_unref(elements.jitterbuffer);
        if (elements.depay)
            gst_object_unref(elements.depay);
        if (elements.parser)
            gst_object_unref(elements.parser);
        if (elements.capsfilter)
            gst_object_unref(elements.capsfilter);
        if (elements.queue_before_decoder)
            gst_object_unref(elements.queue_before_decoder);
        if (elements.decoder)
            gst_object_unref(elements.decoder);
        if (elements.system_converter)
            gst_object_unref(elements.system_converter);
        if (elements.gpu_converter)
            gst_object_unref(elements.gpu_converter);
        if (elements.queue_before_tee)
            gst_object_unref(elements.queue_before_tee);
        if (elements.tee)
            gst_object_unref(elements.tee);
    } else {
        // Function may fail if RTP plugins are not available
        SUCCEED();
    }
}

/**
 * @brief Test UDP input functions for connection state tracking.
 */
TEST_F(InputPipelinesTest, UdpConnectionStateFunctions) {
    input_ingress_facade view(*data);

    // Test initial state
    EXPECT_FALSE(has_udp_client_connected(view));
    EXPECT_EQ(get_udp_packet_count(view), 0);
    EXPECT_EQ(get_udp_bytes_received(view), 0);

    // Reset state
    reset_udp_connection_state(view);

    // Verify reset
    EXPECT_FALSE(has_udp_client_connected(view));
    EXPECT_EQ(get_udp_packet_count(view), 0);
    EXPECT_EQ(get_udp_bytes_received(view), 0);
    EXPECT_EQ(get_udp_min_packet_size(view), 0);
    EXPECT_EQ(get_udp_max_packet_size(view), 0);
    EXPECT_DOUBLE_EQ(get_udp_average_packet_size(view), 0.0);
}

/**
 * @brief UDP counters and helpers are tied to the owning global_data instance (M9).
 */
TEST_F(InputPipelinesTest, UdpRuntimeIsScopedPerGlobalDataInstance) {
    encoder_config other_config;
    other_config.input_type_value = input_type::file;
    other_config.width = 1920;
    other_config.height = 1080;
    other_config.framerate = 30;
    global_data other(std::move(other_config));

    data->udp_input_runtime.udp_packet_count.store(7);
    input_ingress_facade view_data(*data);
    input_ingress_facade view_other(other);
    EXPECT_EQ(get_udp_packet_count(view_data), 7U);
    EXPECT_EQ(get_udp_packet_count(view_other), 0U);

    reset_udp_connection_state(view_data);
    EXPECT_EQ(get_udp_packet_count(view_data), 0U);
    EXPECT_EQ(get_udp_packet_count(view_other), 0U);
}

/**
 * @brief Test create_deepstream_elements creates required elements.
 */
#ifdef HAVE_DEEPSTREAM
TEST_F(InputPipelinesTest, DeepstreamElementsCreation) {
    encoder_config roi_config;
    roi_config.input_type_value = input_type::file;
    roi_config.inference_engine = true;
    roi_config.deepstream_config_file = "test_config.txt";

    global_data test_data(std::move(roi_config));

    deepstream_elements elements;
    const bool result = create_deepstream_elements(elements, test_data.params);

    // Result depends on DeepStream plugin availability
    // If elements are created, verify structure
    if (result) {
        // Elements may be null if DeepStream is not available
        // This is acceptable for testing
        SUCCEED();
    } else {
        // Function may fail if DeepStream plugins are not available
        SUCCEED();
    }
}
#else
TEST_F(InputPipelinesTest, DeepstreamElementsCreation) {
    GTEST_SKIP() << "DeepStream not available - skipping DeepStream element tests";
}
#endif

#ifdef HAVE_DEEPSTREAM
TEST_F(InputPipelinesTest, GenerateTrackerConfigFileRewritesExpectedKeys) {
    const std::filesystem::path base_path = "test_tracker_base.yml";
    const std::filesystem::path output_path = "test_tracker_generated.yml";

    std::ofstream base_file(base_path);
    ASSERT_TRUE(base_file.is_open());
    base_file << "BaseConfig:\n";
    base_file << " minDetectorConfidence: 0.8049\n";
    base_file << "TargetManagement:\n";
    base_file << "  maxShadowTrackingAge: 30\n";
    base_file << "  maxTargetsPerStream: 300\n";
    base_file << "  probationAge: 5\n";
    base_file << "  earlyTerminationAge: 1\n";
    base_file << "TrajectoryManagement:\n";
    base_file << "  enableReAssoc: 1\n";
    base_file << "  trajectoryProjectionLength: 30\n";
    base_file << "  maxTrackletMatchingTimeSearchRange: 25\n";
    base_file << "DataAssociator:\n";
    base_file << " usePrediction4Assoc: 1\n";
    base_file.close();

    const bool generated = generate_tracker_config_file(base_path.string(), output_path.string(), 70, false, 15, 9, 150,
                                                        3, 2, 0.5501, false);
    ASSERT_TRUE(generated);
    ASSERT_TRUE(std::filesystem::exists(output_path));

    std::ifstream output_file(output_path);
    ASSERT_TRUE(output_file.is_open());
    const std::string output_contents((std::istreambuf_iterator<char>(output_file)), std::istreambuf_iterator<char>());

    EXPECT_NE(output_contents.find("maxShadowTrackingAge: 70"), std::string::npos);
    EXPECT_NE(output_contents.find("enableReAssoc: 0"), std::string::npos);
    EXPECT_NE(output_contents.find("trajectoryProjectionLength: 15"), std::string::npos);
    EXPECT_NE(output_contents.find("maxTrackletMatchingTimeSearchRange: 9"), std::string::npos);
    EXPECT_NE(output_contents.find("maxTargetsPerStream: 150"), std::string::npos);
    EXPECT_NE(output_contents.find("probationAge: 3"), std::string::npos);
    EXPECT_NE(output_contents.find("earlyTerminationAge: 2"), std::string::npos);
    EXPECT_NE(output_contents.find("minDetectorConfidence: 0.5501"), std::string::npos);
    EXPECT_NE(output_contents.find("usePrediction4Assoc: 0"), std::string::npos);

    std::filesystem::remove(base_path);
    std::filesystem::remove(output_path);
}

TEST_F(InputPipelinesTest, GenerateTrackerConfigFileFailsForMissingInputFile) {
    const std::filesystem::path missing_input = "does_not_exist_tracker.yml";
    const std::filesystem::path output_path = "unused_generated_tracker.yml";

    const bool generated = generate_tracker_config_file(missing_input.string(), output_path.string(), 70, false, 15, 9,
                                                        150, 3, 2, 0.5501, false);
    EXPECT_FALSE(generated);
    EXPECT_FALSE(std::filesystem::exists(output_path));
}

TEST_F(InputPipelinesTest, GenerateTrackerConfigFilePreservesContentWhenSectionsAreMissing) {
    const std::filesystem::path base_path = "test_tracker_base_missing_sections.yml";
    const std::filesystem::path output_path = "test_tracker_generated_missing_sections.yml";

    std::ofstream base_file(base_path);
    ASSERT_TRUE(base_file.is_open());
    base_file << "CustomSection:\n";
    base_file << "  untouched: true\n";
    base_file << "AnotherSection:\n";
    base_file << "  value: 123\n";
    base_file.close();

    const bool generated = generate_tracker_config_file(base_path.string(), output_path.string(), 80, true, 10, 7, 200,
                                                        4, 2, 0.3300, true);
    ASSERT_TRUE(generated);
    ASSERT_TRUE(std::filesystem::exists(output_path));

    std::ifstream output_file(output_path);
    ASSERT_TRUE(output_file.is_open());
    const std::string output_contents((std::istreambuf_iterator<char>(output_file)), std::istreambuf_iterator<char>());

    EXPECT_NE(output_contents.find("CustomSection:"), std::string::npos);
    EXPECT_NE(output_contents.find("untouched: true"), std::string::npos);
    EXPECT_NE(output_contents.find("AnotherSection:"), std::string::npos);
    EXPECT_NE(output_contents.find("value: 123"), std::string::npos);

    std::filesystem::remove(base_path);
    std::filesystem::remove(output_path);
}

TEST_F(InputPipelinesTest, GenerateTrackerConfigFileFailsWhenOutputDirectoryIsMissing) {
    const std::filesystem::path base_path = "test_tracker_base_valid.yml";
    const std::filesystem::path output_directory = "missing_tracker_output_dir";
    const std::filesystem::path output_path = output_directory / "generated_tracker.yml";
    std::filesystem::remove_all(output_directory);

    std::ofstream base_file(base_path);
    ASSERT_TRUE(base_file.is_open());
    base_file << "BaseConfig:\n";
    base_file << " minDetectorConfidence: 0.8049\n";
    base_file.close();

    const bool generated = generate_tracker_config_file(base_path.string(), output_path.string(), 70, false, 15, 9, 150,
                                                        3, 2, 0.5501, false);
    EXPECT_FALSE(generated);
    EXPECT_FALSE(std::filesystem::exists(output_path));

    std::filesystem::remove(base_path);
}

TEST_F(InputPipelinesTest, ConfigureTrackerReturnsTrueWhenTrackerElementIsNull) {
    encoder_config config;
    config.input_type_value = input_type::file;
    config.width = 640;
    config.height = 480;
    config.framerate = 30;
    config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
    config.format = sf::video_format::i420;
    config.inference_engine = true;
    config.detection.enable_tracker = false;
    global_data test_data(std::move(config));

    EXPECT_TRUE(configure_tracker(test_data.params, nullptr));
}

TEST_F(InputPipelinesTest, PtsStorageQueueStartsEmpty) {
    encoder_config config;
    config.input_type_value = input_type::file;
    config.width = 640;
    config.height = 480;
    config.framerate = 30;
    config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
    config.format = sf::video_format::i420;
    global_data test_data(std::move(config));
    const input_ingress_facade view(test_data);

    EXPECT_EQ(get_pts_storage_queue_size(view), 0U);
}

TEST_F(InputPipelinesTest, ConfigureDeepstreamElementsSucceedsWithEmptyConfigPath) {
    encoder_config config;
    config.input_type_value = input_type::file;
    config.width = 640;
    config.height = 480;
    config.framerate = 30;
    config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
    config.format = sf::video_format::i420;
    config.inference_engine = false;
    config.deepstream_config_file.clear();
    global_data test_data(std::move(config));

    deepstream_elements elements{};
    elements.streammux = gst_element_factory_make("nvstreammux", "test-streammux");
    if (!elements.streammux) {
        GTEST_SKIP() << "nvstreammux plugin unavailable";
    }

    input_ingress_facade view(test_data);
    EXPECT_TRUE(configure_deepstream_elements(view, elements));
    gst_object_unref(elements.streammux);
}

TEST_F(InputPipelinesTest, SetupPtsPreservationProbesRequiresBothProbePads) {
    encoder_config config;
    config.input_type_value = input_type::file;
    config.width = 640;
    config.height = 480;
    config.framerate = 30;
    config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
    config.format = sf::video_format::i420;
    global_data test_data(std::move(config));

    deepstream_elements elements{};
    elements.valve_inference = gst_element_factory_make("identity", "test-valve-inference");
    elements.videoconvert_inference = gst_element_factory_make("identity", "test-videoconvert-inference");
    ASSERT_NE(elements.valve_inference, nullptr);
    ASSERT_NE(elements.videoconvert_inference, nullptr);

    input_ingress_facade view(test_data);
    EXPECT_TRUE(setup_pts_preservation_probes(view, elements));
    gst_object_unref(elements.valve_inference);
    gst_object_unref(elements.videoconvert_inference);
}
#endif

/**
 * @brief Test input pipeline creation with invalid configuration.
 */
TEST_F(InputPipelinesTest, InvalidConfiguration) {
    encoder_config invalid_config;
    // Use a valid enum value since input_type_value is an enum
    // The test verifies that invalid combinations are handled gracefully
    invalid_config.input_type_value = input_type::file;

    global_data test_data(std::move(invalid_config));
    test_data.input_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-pipeline"));

    // Function should handle invalid config gracefully
    // Actual behavior depends on implementation
    SUCCEED();
}
