// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>

#include "../../rtp_server/src/core/rtp_config.h"
#include "../../rtp_server/src/core/rtp_server_config.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    /**
     * @brief Creates a temporary file with the specified content.
     * @param path The path where the file will be created.
     * @param content The string content to write to the file.
     */
    void create_temp_file(const std::filesystem::path &path, const std::string &content) {
        std::ofstream file(path);
        file << content;
    }

    // Test fixture for rtp_server_config tests
    struct rtp_server_config_test : GStreamerTestFixtureRtpServer {
    protected:
        const std::filesystem::path test_dir = "temp_rtp_server_config_test_files";

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            std::filesystem::create_directory(test_dir);
        }

        void TearDown() override {
            std::filesystem::remove_all(test_dir);
            GStreamerTestFixtureRtpServer::TearDown();
        }
    };

    // Test case for default configuration
    TEST_F(rtp_server_config_test, default_configuration) {
        const rtp_server_config config;

        EXPECT_EQ(config.handshake_port, 5000);
        EXPECT_EQ(config.communication_transport_type, transport_type::rtp);
        EXPECT_EQ(config.background_color, sf::background_color::green);
        EXPECT_EQ(config.draw_bitrate, false);
        EXPECT_EQ(config.save_frames, false);
        EXPECT_FALSE(config.output_directory.has_value());
        EXPECT_TRUE(config.groundtruth_file.empty());
        EXPECT_TRUE(config.semseg_deepstream_config_file.empty());
        EXPECT_TRUE(config.semseg_model_file.empty());
        EXPECT_TRUE(config.semseg_engine_file.empty());
        EXPECT_TRUE(config.semseg_labels_file.empty());
        EXPECT_TRUE(config.semseg_profile.empty());
        EXPECT_TRUE(config.semseg_groundtruth_file.empty());
        EXPECT_DOUBLE_EQ(config.test_mode_iou_threshold, 0.5);
        EXPECT_FALSE(config.test_mode_output_md.has_value());
        EXPECT_FALSE(config.test_mode_output_json.has_value());
        EXPECT_FALSE(config.semseg_output_md.has_value());
        EXPECT_FALSE(config.semseg_output_json.has_value());
        EXPECT_FALSE(config.tet_enabled);
        EXPECT_TRUE(config.tet_tile_file.empty());
        EXPECT_EQ(config.tet_qp, 27U);
        EXPECT_FALSE(config.tet_dynamic_qp);
        EXPECT_FALSE(config.decoder_element.has_value());
        EXPECT_TRUE(config.enable_luma_backscale);
    }

    // Test case for creating default config file
    TEST_F(rtp_server_config_test, create_default_config_file) {
        const auto config_path = test_dir / "default_config.ini";

        create_default_rtp_server_config_file(config_path);

        EXPECT_TRUE(std::filesystem::exists(config_path));
        EXPECT_TRUE(std::filesystem::file_size(config_path) > 0);

        // Verify file can be loaded
        const auto loaded_config = load_rtp_server_config(config_path);
        EXPECT_EQ(loaded_config.handshake_port, 5000);
    }

    // Test case for loading valid configuration
    TEST_F(rtp_server_config_test, load_valid_config) {
        const auto config_path = test_dir / "valid_config.ini";
        const std::string config_content = R"(
[Server]
handshake_port=8080

[Common]
background_color=0

[Monitoring]
enable_packet_loss_check=true
max_packet_loss_rate=5.0

[Debug]
save_frames=true

[Inference]
inference_engine=true
deepstream_config_file=../yolo/yolo_config_11x.yml
semseg_deepstream_config_file=../yolo/semseg/semseg_config_default.yml
semseg_model_file=../yolo/semseg/model.onnx
semseg_engine_file=../yolo/semseg/model.engine
semseg_labels_file=../yolo/semseg/labels_vcm_eval.txt
semseg_profile=default
labels=person,car

[TET]
enabled=true
tile_file=tiles/sample.tet
qp=32
dynamic_qp=true

[Decoder]
element=openhevcdec
enable_luma_backscale=false

[TestMode]
groundtruth_file=groundtruth/sample.json
semseg_groundtruth_file=groundtruth/semseg_sparse.json
iou_threshold=0.7
output_md=/tmp/test_mode_summary.md
output_json=/tmp/test_mode_summary.json
semseg_output_md=/tmp/test_mode_semseg_summary.md
semseg_output_json=/tmp/test_mode_semseg_summary.json
)";

        create_temp_file(config_path, config_content);

        const auto config = load_rtp_server_config(config_path);

        EXPECT_EQ(config.handshake_port, 8080);
        EXPECT_EQ(config.background_color, sf::background_color::green);
        EXPECT_TRUE(config.monitoring_config.enable_packet_loss_check);
        EXPECT_DOUBLE_EQ(config.monitoring_config.max_packet_loss_rate, 5.0);
        EXPECT_TRUE(config.save_frames);
        EXPECT_TRUE(config.inference_engine);
        EXPECT_EQ(config.deepstream_config_file, "../yolo/yolo_config_11x.yml");
        EXPECT_EQ(config.semseg_deepstream_config_file, "../yolo/semseg/semseg_config_default.yml");
        EXPECT_EQ(config.semseg_model_file, "../yolo/semseg/model.onnx");
        EXPECT_EQ(config.semseg_engine_file, "../yolo/semseg/model.engine");
        EXPECT_EQ(config.semseg_labels_file, "../yolo/semseg/labels_vcm_eval.txt");
        EXPECT_EQ(config.semseg_profile, "default");
        EXPECT_EQ(config.labels, "person,car");
        EXPECT_TRUE(config.tet_enabled);
        EXPECT_EQ(config.tet_tile_file, "tiles/sample.tet");
        EXPECT_EQ(config.tet_qp, 32U);
        EXPECT_TRUE(config.tet_dynamic_qp);
        ASSERT_TRUE(config.decoder_element.has_value());
        EXPECT_EQ(config.decoder_element.value(), "openhevcdec");
        EXPECT_FALSE(config.enable_luma_backscale);
        EXPECT_EQ(config.groundtruth_file, "groundtruth/sample.json");
        EXPECT_EQ(config.semseg_groundtruth_file, "groundtruth/semseg_sparse.json");
        EXPECT_DOUBLE_EQ(config.test_mode_iou_threshold, 0.7);
        ASSERT_TRUE(config.test_mode_output_md.has_value());
        EXPECT_EQ(config.test_mode_output_md.value(), "/tmp/test_mode_summary.md");
        ASSERT_TRUE(config.test_mode_output_json.has_value());
        EXPECT_EQ(config.test_mode_output_json.value(), "/tmp/test_mode_summary.json");
        ASSERT_TRUE(config.semseg_output_md.has_value());
        EXPECT_EQ(config.semseg_output_md.value(), "/tmp/test_mode_semseg_summary.md");
        ASSERT_TRUE(config.semseg_output_json.has_value());
        EXPECT_EQ(config.semseg_output_json.value(), "/tmp/test_mode_semseg_summary.json");
    }

    // Test case for loading non-existent file (should throw)
    TEST_F(rtp_server_config_test, load_nonexistent_file) {
        const auto config_path = test_dir / "nonexistent.ini";

        EXPECT_THROW(load_rtp_server_config(config_path), std::runtime_error);
    }
} // namespace
