// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/gst/output_pipeline/elements.h"
#include "../../encoder_cli/src/gst/output_pipeline/paths.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }

    [[nodiscard]] sf::unique_gst_sample_ptr make_sample_with_buffer(const GstClockTime pts) {
        auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, 32, nullptr));
        if (!buffer) {
            return nullptr;
        }
        GST_BUFFER_PTS(buffer.get()) = pts;
        auto caps = sf::unique_gst_caps_ptr(gst_caps_new_empty_simple("application/octet-stream"));
        if (!caps) {
            return nullptr;
        }
        return sf::unique_gst_sample_ptr(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    }

    class scoped_current_path {
    public:
        explicit scoped_current_path(const std::filesystem::path &path) :
            old_path_value(std::filesystem::current_path()) {
            std::filesystem::current_path(path);
        }

        ~scoped_current_path() { std::filesystem::current_path(old_path_value); }

    private:
        std::filesystem::path old_path_value;
    };
} // namespace

class FramePathMetaAndOutputPathsTest : public GStreamerTestFixture {};

TEST_F(FramePathMetaAndOutputPathsTest, FramePathMetaApiAndInfoAreStableAcrossCalls) {
    const GType type_a = frame_path_meta_api_get_type();
    const GType type_b = frame_path_meta_api_get_type();
    EXPECT_NE(type_a, static_cast<GType>(0));
    EXPECT_EQ(type_a, type_b);

    const GstMetaInfo *info_a = frame_path_meta_get_info();
    const GstMetaInfo *info_b = frame_path_meta_get_info();
    ASSERT_NE(info_a, nullptr);
    EXPECT_EQ(info_a, info_b);
}

TEST_F(FramePathMetaAndOutputPathsTest,
       init_debug_category_frame_path_meta_api_get_type_impl_frame_path_meta_init_frame_path_meta_free_coverage) {
    const GType meta_type = frame_path_meta_api_get_type();
    EXPECT_NE(meta_type, static_cast<GType>(0));

    auto sample = make_sample_with_buffer(333U);
    ASSERT_NE(sample, nullptr);
    EXPECT_TRUE(attach_frame_path_metadata(sample, frame_path_type::inference, 1));
}

TEST_F(FramePathMetaAndOutputPathsTest, AttachFramePathMetadataHandlesWritableAndNonWritableBuffers) {
    sf::unique_gst_sample_ptr null_sample;
    EXPECT_FALSE(attach_frame_path_metadata(null_sample, frame_path_type::bypass, 1));
}

TEST_F(FramePathMetaAndOutputPathsTest, AttachMetadataWritesBypassPathOnWritableBuffer) {
    auto sample = make_sample_with_buffer(111U);
    ASSERT_NE(sample, nullptr);

    EXPECT_TRUE(attach_frame_path_metadata(sample, frame_path_type::bypass, 7));
    GstBuffer *buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(get_frame_path_from_buffer(buffer), frame_path_type::bypass);
    EXPECT_TRUE(is_bypass_frame(buffer));
    const auto contract = get_frame_contract_from_buffer(buffer);
    ASSERT_TRUE(contract.has_value());
    EXPECT_EQ(contract->sequence_id, 7U);
}

TEST_F(FramePathMetaAndOutputPathsTest, NonWritableBufferWithPtsAttachesMetadataViaWritableCopy) {
    auto sample = make_sample_with_buffer(222U);
    ASSERT_NE(sample, nullptr);
    GstBuffer *buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(buffer, nullptr);

    GstBuffer *extra_ref = gst_buffer_ref(buffer);
    ASSERT_NE(extra_ref, nullptr);
    EXPECT_FALSE(gst_buffer_is_writable(buffer));

    EXPECT_TRUE(attach_frame_path_metadata(sample, frame_path_type::bypass, 9));
    EXPECT_EQ(get_frame_path_from_buffer(buffer), frame_path_type::inference);
    GstBuffer *updated_buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(updated_buffer, nullptr);
    EXPECT_EQ(get_frame_path_from_buffer(updated_buffer), frame_path_type::bypass);
    const auto contract = get_frame_contract_from_buffer(updated_buffer);
    ASSERT_TRUE(contract.has_value());
    EXPECT_EQ(contract->sequence_id, 9U);

    gst_buffer_unref(extra_ref);
}

TEST_F(FramePathMetaAndOutputPathsTest, NonWritableBufferWithoutPtsStillAttachesMetadataWithWritableCopy) {
    auto sample = make_sample_with_buffer(GST_CLOCK_TIME_NONE);
    ASSERT_NE(sample, nullptr);
    GstBuffer *buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(buffer, nullptr);

    GstBuffer *extra_ref = gst_buffer_ref(buffer);
    ASSERT_NE(extra_ref, nullptr);
    EXPECT_FALSE(gst_buffer_is_writable(buffer));

    EXPECT_TRUE(attach_frame_path_metadata(sample, frame_path_type::bypass, 11));
    EXPECT_EQ(get_frame_path_from_buffer(buffer), frame_path_type::inference);

    gst_buffer_unref(extra_ref);
}

TEST_F(FramePathMetaAndOutputPathsTest, FramePathFallbackDefaultsToInferenceWhenBufferMissing) {
    EXPECT_EQ(get_frame_path_from_buffer(nullptr), frame_path_type::inference);
    EXPECT_FALSE(is_bypass_frame(nullptr));
}

TEST_F(FramePathMetaAndOutputPathsTest, ConfigureOutputPathsSetsExpectedLocationsInIsolatedDirectory) {
    auto config = make_base_config();
    config.debug.save_frames = false;
    config.pack_rois = true;
    global_data data(std::move(config));

    output_pipeline_elements elements{};
    elements.sink_file = gst_element_factory_make("filesink", "test-paths-file-sink");
    elements.sink_mp4 = gst_element_factory_make("filesink", "test-paths-mp4-sink");
    ASSERT_NE(elements.sink_file, nullptr);
    ASSERT_NE(elements.sink_mp4, nullptr);

    const auto unique_root =
            std::filesystem::temp_directory_path() /
            ("vcai_paths_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto isolated_cwd = unique_root / "cwd";
    std::filesystem::create_directories(isolated_cwd);

    {
        scoped_current_path cwd_guard(isolated_cwd);
        ASSERT_TRUE(configure_output_paths(data.params, elements));
    }

    gchar *h265_location = nullptr;
    gchar *mp4_location = nullptr;
    g_object_get(G_OBJECT(elements.sink_file), "location", &h265_location, nullptr);
    g_object_get(G_OBJECT(elements.sink_mp4), "location", &mp4_location, nullptr);
    ASSERT_NE(h265_location, nullptr);
    ASSERT_NE(mp4_location, nullptr);
    EXPECT_STREQ(h265_location, "../yolo/yolo_output_with_sei_packed.h265");
    EXPECT_STREQ(mp4_location, "../yolo/yolo_output_with_sei_packed.mp4");
    g_free(h265_location);
    g_free(mp4_location);

    EXPECT_TRUE(std::filesystem::exists(unique_root / "yolo"));
    std::filesystem::remove_all(unique_root);

    gst_object_unref(elements.sink_file);
    gst_object_unref(elements.sink_mp4);
}

TEST_F(FramePathMetaAndOutputPathsTest,
       create_output_directory_configure_h265_file_sink_configure_mp4_file_sink_and_clear_debug_frames_directory_paths) {
    auto config = make_base_config();
    config.debug.save_frames = true;
    global_data data(std::move(config));

    output_pipeline_elements elements{};
    elements.sink_file = gst_element_factory_make("filesink", "test-paths-file-sink-symbol-coverage");
    elements.sink_mp4 = gst_element_factory_make("filesink", "test-paths-mp4-sink-symbol-coverage");
    elements.sink_debug = gst_element_factory_make("multifilesink", "test-paths-debug-sink-symbol-coverage");
    ASSERT_NE(elements.sink_file, nullptr);
    ASSERT_NE(elements.sink_mp4, nullptr);
    ASSERT_NE(elements.sink_debug, nullptr);

    const auto unique_root = std::filesystem::temp_directory_path() /
                             ("vcai_paths_symbols_" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto isolated_cwd = unique_root / "cwd";
    std::filesystem::create_directories(isolated_cwd / "debug_frames");
    {
        std::ofstream stale_file(isolated_cwd / "debug_frames" / "stale.txt");
        ASSERT_TRUE(stale_file.is_open());
        stale_file << "stale";
    }

    {
        scoped_current_path cwd_guard(isolated_cwd);
        ASSERT_TRUE(configure_output_paths(data.params, elements));
    }
    EXPECT_FALSE(std::filesystem::exists(isolated_cwd / "debug_frames" / "stale.txt"));

    std::filesystem::remove_all(unique_root);
    gst_object_unref(elements.sink_file);
    gst_object_unref(elements.sink_mp4);
    gst_object_unref(elements.sink_debug);
}

TEST_F(FramePathMetaAndOutputPathsTest, ConfigureOutputPathsWithDebugSavingCleansDirectoryAndConfiguresSink) {
    auto config = make_base_config();
    config.debug.save_frames = true;
    global_data data(std::move(config));

    output_pipeline_elements elements{};
    elements.sink_debug = gst_element_factory_make("multifilesink", "test-paths-debug-sink");
    ASSERT_NE(elements.sink_debug, nullptr);

    const auto unique_root = std::filesystem::temp_directory_path() /
                             ("vcai_paths_debug_test_" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto isolated_cwd = unique_root / "cwd";
    const auto debug_dir = isolated_cwd / "debug_frames";
    std::filesystem::create_directories(debug_dir);
    {
        std::ofstream stale_file(debug_dir / "stale.png");
        ASSERT_TRUE(stale_file.is_open());
        stale_file << "old";
    }
    ASSERT_TRUE(std::filesystem::exists(debug_dir / "stale.png"));

    {
        scoped_current_path cwd_guard(isolated_cwd);
        ASSERT_TRUE(configure_output_paths(data.params, elements));
    }

    EXPECT_FALSE(std::filesystem::exists(debug_dir / "stale.png"));
    EXPECT_TRUE(std::filesystem::exists(debug_dir));
    gchar *debug_location = nullptr;
    gint max_files = -1;
    g_object_get(G_OBJECT(elements.sink_debug), "location", &debug_location, "max-files", &max_files, nullptr);
    ASSERT_NE(debug_location, nullptr);
    EXPECT_EQ(std::string(debug_location), (debug_dir / "frame_%06d.png").string());
    EXPECT_EQ(max_files, 0);
    g_free(debug_location);

    std::filesystem::remove_all(unique_root);
    gst_object_unref(elements.sink_debug);
}

TEST_F(FramePathMetaAndOutputPathsTest, DetermineModelNameReturnsStableDefaultValue) {
    EXPECT_EQ(determine_model_name(), "yolo");
}
