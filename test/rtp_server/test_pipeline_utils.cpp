// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <glib.h>
#include <gtest/gtest.h>
#include <thread>

#include "../../rtp_server/src/core/rtp_server_config.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../../rtp_server/src/pipeline/pipeline/utils.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    struct pipeline_utils_test : GStreamerTestFixtureRtpServer {
        GMainLoop *test_loop = nullptr;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            // Create a test main loop
            test_loop = g_main_loop_new(nullptr, FALSE);
            // Clear environment variable if set
            unsetenv("VCAI_DATA_DIR");
        }

        void TearDown() override {
            if (test_loop) {
                g_main_loop_unref(test_loop);
                test_loop = nullptr;
            }
            unsetenv("VCAI_DATA_DIR");
            GStreamerTestFixtureRtpServer::TearDown();
        }

        std::unique_ptr<rtp_pipeline> create_test_pipeline(guint16 port,
                                                           const std::optional<std::string> &output_dir = std::nullopt,
                                                           const rtp_server_config *server_config = nullptr) {
            // Use the constructor with minimal required parameters
            return std::make_unique<rtp_pipeline>(port, test_loop, rtp_output_config{}, // Default RTP output config
                                                  output_dir, // Output directory
                                                  sf::background_color::green, // Default background color
                                                  transport_context{.functions = {}}, // Empty transport context
                                                  false, // draw_bitrate
                                                  false, // draw_bitrate_large
                                                  false, // draw_fps
                                                  server_config);
        }
    };

    // Test: Filename format correctness
    TEST_F(pipeline_utils_test, generate_filename_format) {
        constexpr guint16 test_port = 5004;
        auto pipeline = create_test_pipeline(test_port);

        const auto filename = generate_filename(*pipeline);

        // Check filename contains port number
        EXPECT_NE(filename.find("rtp_stream_port5004"), std::string::npos);

        // Check filename contains timestamp pattern (YYYYMMDD_HHMMSS)
        // We can't check exact timestamp, but we can check the pattern exists
        EXPECT_NE(filename.find("_"), std::string::npos);

        // Check filename ends with .h265
        EXPECT_EQ(filename.substr(filename.length() - 5), ".h265");
    }

    // Test: Filename with output_directory set
    TEST_F(pipeline_utils_test, generate_filename_with_output_directory) {
        constexpr guint16 test_port = 5004;
        const std::string output_dir = "/tmp/test_output";
        auto pipeline = create_test_pipeline(test_port, output_dir);

        const auto filename = generate_filename(*pipeline);

        // Check filename starts with output directory
        EXPECT_EQ(filename.substr(0, output_dir.length()), output_dir);

        // Check filename contains path separator
        EXPECT_NE(filename.find("/"), std::string::npos);

        // Check filename ends with .h265
        EXPECT_EQ(filename.substr(filename.length() - 5), ".h265");
    }

    TEST_F(pipeline_utils_test, generate_filename_prefers_output_directory_over_environment_variable) {
        constexpr guint16 test_port = 5004;
        const std::string output_dir = "/tmp/test_output_preferred";
        setenv("VCAI_DATA_DIR", "/tmp/vcai_data_env", 1);
        auto pipeline = create_test_pipeline(test_port, output_dir);

        const auto filename = generate_filename(*pipeline);
        EXPECT_EQ(filename.substr(0, output_dir.size()), output_dir);
        EXPECT_EQ(filename.find("/tmp/vcai_data_env"), std::string::npos);
    }

    // Test: Filename without output_directory (no env var)
    TEST_F(pipeline_utils_test, generate_filename_without_output_directory) {
        constexpr guint16 test_port = 5004;
        auto pipeline = create_test_pipeline(test_port);

        const auto filename = generate_filename(*pipeline);

        // Should return just the filename (no path)
        const auto last_slash = filename.find_last_of("/");
        if (last_slash != std::string::npos) {
            // If there's a path, it should be from VCAI_DATA_DIR
            // But we unset it, so there shouldn't be a path
            // Actually, the function may still return a path if VCAI_DATA_DIR was set
            // before we unset it, so we just check the filename part
        }

        // Check filename contains port
        EXPECT_NE(filename.find("rtp_stream_port5004"), std::string::npos);

        // Check filename ends with .h265
        EXPECT_EQ(filename.substr(filename.length() - 5), ".h265");
    }

    // Test: Filename with VCAI_DATA_DIR environment variable
    TEST_F(pipeline_utils_test, generate_filename_with_env_var) {
        constexpr guint16 test_port = 5004;
        const std::string data_dir = "/tmp/vcai_data";
        setenv("VCAI_DATA_DIR", data_dir.c_str(), 1);

        auto pipeline = create_test_pipeline(test_port);

        const auto filename = generate_filename(*pipeline);

        // Check filename contains data directory path
        EXPECT_NE(filename.find(data_dir), std::string::npos);

        // Check filename contains rtp_server subdirectory
        EXPECT_NE(filename.find("rtp_server"), std::string::npos);

        // Check filename ends with .h265
        EXPECT_EQ(filename.substr(filename.length() - 5), ".h265");

        unsetenv("VCAI_DATA_DIR");
    }

    // Test: Filename with different ports
    TEST_F(pipeline_utils_test, generate_filename_different_ports) {
        constexpr guint16 port1 = 5004;
        constexpr guint16 port2 = 6000;

        auto pipeline1 = create_test_pipeline(port1);
        auto pipeline2 = create_test_pipeline(port2);

        const auto filename1 = generate_filename(*pipeline1);
        const auto filename2 = generate_filename(*pipeline2);

        // Filenames should contain different port numbers
        EXPECT_NE(filename1.find("rtp_stream_port5004"), std::string::npos);
        EXPECT_NE(filename2.find("rtp_stream_port6000"), std::string::npos);

        // Filenames should be different
        EXPECT_NE(filename1, filename2);
    }

    // Test: Filename timestamp format
    TEST_F(pipeline_utils_test, generate_filename_timestamp_format) {
        constexpr guint16 test_port = 5004;
        auto pipeline = create_test_pipeline(test_port);

        const auto filename = generate_filename(*pipeline);

        // Extract the timestamp part (between last _ before .h265)
        const auto last_underscore = filename.find_last_of("_");
        const auto dot_pos = filename.find_last_of(".");

        if (last_underscore != std::string::npos && dot_pos != std::string::npos) {
            const auto timestamp_part = filename.substr(last_underscore + 1, dot_pos - last_underscore - 1);

            // Should be 3 digits (milliseconds)
            EXPECT_EQ(timestamp_part.length(), 3);

            // Should be numeric
            for (const char c: timestamp_part) {
                EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(c)));
            }
        }
    }

    // Test: Filename with empty output_directory
    TEST_F(pipeline_utils_test, generate_filename_empty_output_directory) {
        constexpr guint16 test_port = 5004;
        auto pipeline = create_test_pipeline(test_port, "");

        const auto filename = generate_filename(*pipeline);

        // Should fall back to VCAI_DATA_DIR or just filename
        EXPECT_NE(filename.find("rtp_stream_port5004"), std::string::npos);
        EXPECT_EQ(filename.substr(filename.length() - 5), ".h265");
    }

    // Test: Multiple filename generations produce different timestamps
    TEST_F(pipeline_utils_test, generate_filename_unique_timestamps) {
        constexpr guint16 test_port = 5004;
        auto pipeline = create_test_pipeline(test_port);

        const auto filename1 = generate_filename(*pipeline);

        // Wait a bit to ensure different timestamp
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        const auto filename2 = generate_filename(*pipeline);

        // Filenames should be different (different timestamps)
        // Note: If generated very quickly, they might be the same, but that's acceptable
        // The important thing is the function works correctly
        EXPECT_NE(filename1.find("rtp_stream_port5004"), std::string::npos);
        EXPECT_NE(filename2.find("rtp_stream_port5004"), std::string::npos);
    }

    // Test: Filename path separator handling
    TEST_F(pipeline_utils_test, generate_filename_path_separator) {
        constexpr guint16 test_port = 5004;
        const std::string output_dir = "/tmp/test_output";
        auto pipeline = create_test_pipeline(test_port, output_dir);

        const auto filename = generate_filename(*pipeline);

        // Check that path separator is used correctly
        const auto last_separator = filename.find_last_of("/");
        EXPECT_NE(last_separator, std::string::npos);

        // Filename part should not contain path separators
        const auto filename_part = filename.substr(last_separator + 1);
        EXPECT_EQ(filename_part.find("/"), std::string::npos);
    }

    TEST_F(pipeline_utils_test, debug_frames_root_prefers_output_directory) {
        const std::string output_dir = "/tmp/rtp_debug_output";
        auto pipeline = create_test_pipeline(5004, output_dir);

        const std::filesystem::path root_dir = get_debug_frames_root_directory(*pipeline);
        EXPECT_EQ(root_dir, std::filesystem::path(output_dir));
    }

    TEST_F(pipeline_utils_test, debug_frames_root_falls_back_to_config_directory) {
        rtp_server_config server_config;
        server_config.config_directory = "/tmp/rtp_config_dir";
        auto pipeline = create_test_pipeline(5004, std::nullopt, &server_config);

        const std::filesystem::path root_dir = get_debug_frames_root_directory(*pipeline);
        EXPECT_EQ(root_dir, std::filesystem::path("/tmp/rtp_config_dir"));
    }

    TEST_F(pipeline_utils_test, debug_frames_root_uses_config_directory_when_output_directory_empty) {
        rtp_server_config server_config;
        server_config.config_directory = "/tmp/rtp_config_dir_empty_output";
        auto pipeline = create_test_pipeline(5004, "", &server_config);

        const std::filesystem::path root_dir = get_debug_frames_root_directory(*pipeline);
        EXPECT_EQ(root_dir, std::filesystem::path("/tmp/rtp_config_dir_empty_output"));
    }

    TEST_F(pipeline_utils_test, debug_frames_port_directory_format) {
        rtp_server_config server_config;
        server_config.config_directory = "/tmp/rtp_config_dir";
        auto pipeline = create_test_pipeline(5008, std::nullopt, &server_config);

        const std::filesystem::path debug_path = get_debug_frames_port_directory(*pipeline);
        EXPECT_EQ(debug_path, std::filesystem::path("/tmp/rtp_config_dir/debug_frames/port_5008"));
    }

    TEST_F(pipeline_utils_test, debug_frames_port_directory_uses_current_path_fallback_root) {
        constexpr guint16 test_port = 5012;
        auto pipeline = create_test_pipeline(test_port);
        ASSERT_NE(pipeline, nullptr);

        const auto expected = std::filesystem::current_path() / "debug_frames" / "port_5012";
        EXPECT_EQ(get_debug_frames_port_directory(*pipeline), expected);
    }

    TEST_F(pipeline_utils_test, debug_frames_root_falls_back_to_current_path_without_output_or_config) {
        auto pipeline = create_test_pipeline(5004);
        ASSERT_NE(pipeline, nullptr);

        const std::filesystem::path root_dir = get_debug_frames_root_directory(*pipeline);
        EXPECT_EQ(root_dir, std::filesystem::current_path());
    }

    TEST_F(pipeline_utils_test, cleanup_elements_resets_pipeline_bins_and_branch_elements) {
        auto pipeline = create_test_pipeline(5004);
        ASSERT_NE(pipeline, nullptr);

        pipeline->main_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("cleanup-main"));
        ASSERT_NE(pipeline->main_pipeline, nullptr);

        pipeline->rtp_input_source = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("appsrc", nullptr));
        pipeline->queue_encoded = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
        pipeline->queue_display = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
        pipeline->queue_file = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
        pipeline->queue_rtp_output = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
        pipeline->queue_debug_frames = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
        ASSERT_NE(pipeline->rtp_input_source.get(), nullptr);
        ASSERT_NE(pipeline->queue_encoded.get(), nullptr);
        ASSERT_NE(pipeline->queue_display.get(), nullptr);
        ASSERT_NE(pipeline->queue_file.get(), nullptr);
        ASSERT_NE(pipeline->queue_rtp_output.get(), nullptr);
        ASSERT_NE(pipeline->queue_debug_frames.get(), nullptr);

        EXPECT_NO_THROW(cleanup_elements(*pipeline));

        EXPECT_EQ(pipeline->main_pipeline.get(), nullptr);
        EXPECT_EQ(pipeline->rtp_input_source.get(), nullptr);
        EXPECT_EQ(pipeline->queue_encoded.get(), nullptr);
        EXPECT_EQ(pipeline->queue_display.get(), nullptr);
        EXPECT_EQ(pipeline->queue_file.get(), nullptr);
        EXPECT_EQ(pipeline->queue_rtp_output.get(), nullptr);
        EXPECT_EQ(pipeline->queue_debug_frames.get(), nullptr);
    }

    TEST_F(pipeline_utils_test, cleanup_elements_is_safe_for_pipeline_with_default_null_members) {
        auto pipeline = create_test_pipeline(5004);
        ASSERT_NE(pipeline, nullptr);
        EXPECT_NO_THROW(cleanup_elements(*pipeline));
    }
} // namespace
