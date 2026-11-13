// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "../../rtp_server/src/core/inference_engine_startup.h"
#include "../../rtp_server/src/core/rtp_server_config.h"

namespace {
    class inference_engine_startup_test : public ::testing::Test {
    protected:
        std::filesystem::path temp_dir;

        void SetUp() override {
            const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            temp_dir = std::filesystem::temp_directory_path() / ("rtp_server_inference_startup_" + suffix);
            std::filesystem::create_directories(temp_dir);
        }

        void TearDown() override { std::filesystem::remove_all(temp_dir); }

        void write_text_file(const std::filesystem::path &path, const std::string &contents) {
            std::ofstream stream(path);
            ASSERT_TRUE(stream.is_open());
            stream << contents;
        }
    };
} // namespace

TEST_F(inference_engine_startup_test, returns_true_when_inference_is_disabled) {
    rtp_server_config config{};
    config.inference_engine = false;
    EXPECT_TRUE(ensure_inference_engine_ready(config));
}

TEST_F(inference_engine_startup_test, rejects_missing_deepstream_configuration_path) {
    rtp_server_config config{};
    config.inference_engine = true;
    config.deepstream_config_file.clear();
    EXPECT_FALSE(ensure_inference_engine_ready(config));
}

TEST_F(inference_engine_startup_test, rejects_nonexistent_deepstream_config_file) {
    rtp_server_config config{};
    config.inference_engine = true;
    config.deepstream_config_file = (temp_dir / "missing_deepstream_config.yml").string();
    EXPECT_FALSE(ensure_inference_engine_ready(config));
}

TEST_F(inference_engine_startup_test, rejects_nvinfer_config_without_required_engine_or_onnx_keys) {
    const auto deepstream_config = temp_dir / "deepstream.yml";
    const auto nvinfer_config = temp_dir / "nvinfer.txt";

    write_text_file(deepstream_config, "config-file-path=nvinfer.txt\n");
    write_text_file(nvinfer_config, "onnx-file=model.onnx\n");

    rtp_server_config config{};
    config.inference_engine = true;
    config.deepstream_config_file = deepstream_config.string();
    EXPECT_FALSE(ensure_inference_engine_ready(config));
}

TEST_F(inference_engine_startup_test, returns_true_when_engine_file_already_exists) {
    const auto deepstream_config = temp_dir / "deepstream.yml";
    const auto nvinfer_config = temp_dir / "nvinfer.txt";
    const auto engine_file = temp_dir / "model.engine";

    write_text_file(deepstream_config, "config-file-path=nvinfer.txt\n");
    write_text_file(nvinfer_config, "model-engine-file=model.engine\n"
                                    "onnx-file=missing_model.onnx\n");
    write_text_file(engine_file, "engine-bytes");

    rtp_server_config config{};
    config.inference_engine = true;
    config.deepstream_config_file = deepstream_config.string();
    EXPECT_TRUE(ensure_inference_engine_ready(config));
}

TEST_F(inference_engine_startup_test, rejects_missing_onnx_file_when_engine_needs_to_be_built) {
    const auto deepstream_config = temp_dir / "deepstream.yml";
    const auto nvinfer_config = temp_dir / "nvinfer.txt";

    write_text_file(deepstream_config, "config-file-path=nvinfer.txt\n");
    write_text_file(nvinfer_config, "model-engine-file=model.engine\n"
                                    "onnx-file=missing_model.onnx\n");

    rtp_server_config config{};
    config.inference_engine = true;
    config.deepstream_config_file = deepstream_config.string();
    EXPECT_FALSE(ensure_inference_engine_ready(config));
}
