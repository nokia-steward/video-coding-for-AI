// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

#include <gst/video/video-info.h>
#include <gst/video/video.h>

#include "../../libvcai/include/encoder_optimization_info.h"
#include "../../libvcai/include/gst_raii.h"
#include "../../libvcai/include/luma_adaptation_math.h"
#include "../../libvcai/include/packed_regions_info.h"
#include "../../rtp_server/src/pipeline/decoded_frame_memory.h"
#include "../../rtp_server/src/pipeline/decoded_frame_pipeline.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../common/gst_test_fixture_rtp_server.h"

[[nodiscard]] bool apply_luma_backscaling_to_sample(const sf::unique_gst_sample_ptr &sample, guint64 frame_number,
                                                    guint16 port, int ratio_percentage, int backscale_ratio_value) {
    if (!sample) {
        return false;
    }

    const auto backscale_multiplier = sf::compute_luma_backscale_multiplier(ratio_percentage, backscale_ratio_value);
    if (!backscale_multiplier.has_value()) {
        return true;
    }

    auto *buffer = gst_sample_get_buffer(sample.get());
    const auto *caps = gst_sample_get_caps(sample.get());
    if (!buffer || !caps) {
        return false;
    }

    GstVideoInfo video_info;
    gst_video_info_init(&video_info);
    if (!gst_video_info_from_caps(&video_info, caps)) {
        return false;
    }

    GstVideoFrame frame;
    if (!gst_video_frame_map(&frame, &video_info, buffer, GST_MAP_READWRITE)) {
        return false;
    }

    auto *luma_plane = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
    if (!luma_plane) {
        gst_video_frame_unmap(&frame);
        return false;
    }

    const gint luma_height = GST_VIDEO_INFO_COMP_HEIGHT(&video_info, 0);
    const gint luma_width = GST_VIDEO_INFO_COMP_WIDTH(&video_info, 0);
    const gint luma_stride = GST_VIDEO_INFO_PLANE_STRIDE(&video_info, 0);
    const gint luma_pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&video_info, 0);

    for (gint row = 0; row < luma_height; ++row) {
        auto *row_ptr = luma_plane + static_cast<gsize>(row) * static_cast<gsize>(luma_stride);
        for (gint col = 0; col < luma_width; ++col) {
            const gsize index = static_cast<gsize>(col) * static_cast<gsize>(luma_pixel_stride);
            const auto original = static_cast<double>(row_ptr[index]);
            const auto scaled = static_cast<int>(std::floor(original * *backscale_multiplier));
            row_ptr[index] = static_cast<guint8>(std::clamp(scaled, 0, 255));
        }
    }

    gst_video_frame_unmap(&frame);
    return true;
}

namespace {
    [[nodiscard]] sf::unique_gst_sample_ptr make_pattern_i420_sample(const gint width, const gint height,
                                                                     const std::vector<guint8> &y_values,
                                                                     guint8 chroma_u_value, guint8 chroma_v_value);

    struct decoded_frame_pipeline_test : GStreamerTestFixtureRtpServer {
        GMainLoop *test_loop = nullptr;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            test_loop = g_main_loop_new(nullptr, FALSE);
        }

        void TearDown() override {
            if (test_loop != nullptr) {
                g_main_loop_unref(test_loop);
                test_loop = nullptr;
            }
            GStreamerTestFixtureRtpServer::TearDown();
        }

        [[nodiscard]] std::unique_ptr<rtp_pipeline> make_pipeline() const {
            return std::make_unique<rtp_pipeline>(5004, test_loop, rtp_output_config{}, std::nullopt,
                                                  sf::background_color::green, transport_context{.functions = {}},
                                                  false, false, false, nullptr);
        }
    };

    [[nodiscard]] sf::unique_gst_caps_ptr make_raw_caps(const gchar *format, const gint width, const gint height,
                                                        const gint fps_num, const gint fps_den) {
        return sf::unique_gst_caps_ptr(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, format, "width",
                                                           G_TYPE_INT, width, "height", G_TYPE_INT, height, "framerate",
                                                           GST_TYPE_FRACTION, fps_num, fps_den, nullptr));
    }

    void add_test_video_metadata(GstCaps *caps) {
        GstStructure *structure = gst_caps_get_structure(caps, 0);
        if (!structure) {
            return;
        }

        gst_structure_set(structure, "colorimetry", G_TYPE_STRING, "bt709", "chroma-site", G_TYPE_STRING, "mpeg2",
                          "interlace-mode", G_TYPE_STRING, "progressive", "pixel-aspect-ratio", GST_TYPE_FRACTION, 4, 3,
                          nullptr);
    }

    void add_test_nvbuf_cuda_device_memory(GstCaps *caps) {
        GstStructure *structure = gst_caps_get_structure(caps, 0);
        if (!structure) {
            return;
        }

        gst_structure_set(structure, "nvbuf-memory-type", G_TYPE_STRING, "nvbuf-mem-cuda-device", nullptr);
    }

    void add_test_nvmm_memory_feature(GstCaps *caps) {
        gst_caps_set_features(caps, 0, gst_caps_features_new("memory:NVMM", nullptr));
    }

    void expect_test_video_metadata(const GstStructure *structure) {
        ASSERT_NE(structure, nullptr);

        const gchar *colorimetry = gst_structure_get_string(structure, "colorimetry");
        ASSERT_NE(colorimetry, nullptr);
        EXPECT_STREQ(colorimetry, "bt709");

        const gchar *chroma_site = gst_structure_get_string(structure, "chroma-site");
        ASSERT_NE(chroma_site, nullptr);
        EXPECT_STREQ(chroma_site, "mpeg2");

        const gchar *interlace_mode = gst_structure_get_string(structure, "interlace-mode");
        ASSERT_NE(interlace_mode, nullptr);
        EXPECT_STREQ(interlace_mode, "progressive");

        gint pixel_aspect_ratio_num = 0;
        gint pixel_aspect_ratio_den = 1;
        ASSERT_TRUE(gst_structure_get_fraction(structure, "pixel-aspect-ratio", &pixel_aspect_ratio_num,
                                               &pixel_aspect_ratio_den));
        EXPECT_EQ(pixel_aspect_ratio_num, 4);
        EXPECT_EQ(pixel_aspect_ratio_den, 3);
    }

    void expect_no_gpu_memory_metadata(const GstCaps *caps) {
        ASSERT_NE(caps, nullptr);
        EXPECT_FALSE(is_cuda_memory_caps(caps));

        const GstStructure *structure = gst_caps_get_structure(caps, 0);
        ASSERT_NE(structure, nullptr);
        EXPECT_EQ(gst_structure_get_string(structure, "memory"), nullptr);
        EXPECT_EQ(gst_structure_get_string(structure, "nvbuf-memory-type"), nullptr);

        const GstCapsFeatures *features = gst_caps_get_features(caps, 0);
        if (!features) {
            return;
        }

        EXPECT_FALSE(gst_caps_features_contains(features, "memory:NVMM"));
        EXPECT_FALSE(gst_caps_features_contains(features, "memory:CUDAMemory"));
        EXPECT_FALSE(gst_caps_features_contains(features, "memory:CUDA"));
    }

    [[nodiscard]] sf::unique_gst_sample_ptr make_i420_sample(const gint width, const gint height,
                                                             const bool cuda_memory_caps = false) {
        auto caps = make_raw_caps("I420", width, height, 30, 1);
        if (cuda_memory_caps) {
            GstStructure *structure = gst_caps_get_structure(caps.get(), 0);
            gst_structure_set(structure, "memory", G_TYPE_STRING, "CUDAMemory", nullptr);
        }

        GstVideoInfo info;
        gst_video_info_init(&info);
        if (!gst_video_info_from_caps(&info, caps.get())) {
            return nullptr;
        }

        auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, info.size, nullptr));
        if (!buffer) {
            return nullptr;
        }

        GstMapInfo map_info;
        if (!gst_buffer_map(buffer.get(), &map_info, GST_MAP_WRITE)) {
            return nullptr;
        }
        for (gsize index = 0; index < map_info.size; ++index) {
            map_info.data[index] = static_cast<guint8>(index % 251);
        }
        gst_buffer_unmap(buffer.get(), &map_info);

        return sf::unique_gst_sample_ptr(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    }

    [[nodiscard]] sf::unique_gst_sample_ptr make_constant_i420_sample(const gint width, const gint height,
                                                                      const guint8 luma_value,
                                                                      const guint8 chroma_u_value,
                                                                      const guint8 chroma_v_value) {
        const size_t luma_sample_count = static_cast<size_t>(width) * static_cast<size_t>(height);
        return make_pattern_i420_sample(width, height, std::vector<guint8>(luma_sample_count, luma_value),
                                        chroma_u_value, chroma_v_value);
    }

    [[nodiscard]] sf::unique_gst_sample_ptr make_pattern_i420_sample(const gint width, const gint height,
                                                                     const std::vector<guint8> &y_values,
                                                                     const guint8 chroma_u_value,
                                                                     const guint8 chroma_v_value) {
        auto sample = make_i420_sample(width, height, false);
        if (!sample) {
            return nullptr;
        }

        auto *buffer = gst_sample_get_buffer(sample.get());
        const auto *caps = gst_sample_get_caps(sample.get());
        if (!buffer || !caps) {
            return nullptr;
        }

        GstVideoInfo info;
        gst_video_info_init(&info);
        if (!gst_video_info_from_caps(&info, caps)) {
            return nullptr;
        }
        const size_t expected_luma_sample_count = static_cast<size_t>(GST_VIDEO_INFO_COMP_WIDTH(&info, 0)) *
                                                  static_cast<size_t>(GST_VIDEO_INFO_COMP_HEIGHT(&info, 0));
        if (y_values.size() != expected_luma_sample_count) {
            return nullptr;
        }

        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_WRITE)) {
            return nullptr;
        }

        for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES(&info); ++plane) {
            const gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&info, plane);
            const gint plane_width = GST_VIDEO_INFO_COMP_WIDTH(&info, plane);
            const gint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, plane);
            const gint pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&info, plane);
            auto *plane_data = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, plane));
            if (!plane_data) {
                gst_video_frame_unmap(&frame);
                return nullptr;
            }
            for (gint row = 0; row < plane_height; ++row) {
                auto *row_ptr = plane_data + static_cast<gsize>(row) * static_cast<gsize>(plane_stride);
                for (gint col = 0; col < plane_width; ++col) {
                    const gsize pixel_index = static_cast<gsize>(col) * static_cast<gsize>(pixel_stride);
                    if (plane == 0) {
                        const size_t luma_index =
                                static_cast<size_t>(row) * static_cast<size_t>(plane_width) + static_cast<size_t>(col);
                        row_ptr[pixel_index] = y_values[luma_index];
                    } else {
                        row_ptr[pixel_index] = plane == 1 ? chroma_u_value : chroma_v_value;
                    }
                }
            }
        }

        gst_video_frame_unmap(&frame);
        return sample;
    }

    [[nodiscard]] std::optional<std::vector<guint8>> read_i420_luma_values(const sf::unique_gst_sample_ptr &sample) {
        if (!sample) {
            return std::nullopt;
        }

        auto *buffer = gst_sample_get_buffer(sample.get());
        const auto *caps = gst_sample_get_caps(sample.get());
        if (!buffer || !caps) {
            return std::nullopt;
        }

        GstVideoInfo info;
        gst_video_info_init(&info);
        if (!gst_video_info_from_caps(&info, caps)) {
            return std::nullopt;
        }
        if (GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_I420) {
            return std::nullopt;
        }

        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
            return std::nullopt;
        }

        const auto *y_plane = static_cast<const guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
        if (!y_plane) {
            gst_video_frame_unmap(&frame);
            return std::nullopt;
        }

        const gint luma_height = GST_VIDEO_INFO_COMP_HEIGHT(&info, 0);
        const gint luma_width = GST_VIDEO_INFO_COMP_WIDTH(&info, 0);
        const gint luma_stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
        const gint luma_pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&info, 0);

        std::vector<guint8> luma_values;
        luma_values.reserve(static_cast<size_t>(luma_height) * static_cast<size_t>(luma_width));
        for (gint row = 0; row < luma_height; ++row) {
            const auto *row_ptr = y_plane + static_cast<gsize>(row) * static_cast<gsize>(luma_stride);
            for (gint col = 0; col < luma_width; ++col) {
                const gsize index = static_cast<gsize>(col) * static_cast<gsize>(luma_pixel_stride);
                luma_values.push_back(row_ptr[index]);
            }
        }

        gst_video_frame_unmap(&frame);
        return luma_values;
    }

    [[nodiscard]] std::optional<double> compute_i420_luma_mean(const sf::unique_gst_sample_ptr &sample) {
        const auto luma_values = read_i420_luma_values(sample);
        if (!luma_values.has_value() || luma_values->empty()) {
            return std::nullopt;
        }
        long double sum = 0.0;
        for (const auto value: *luma_values) {
            sum += static_cast<long double>(value);
        }
        return static_cast<double>(sum / static_cast<long double>(luma_values->size()));
    }

    [[nodiscard]] bool apply_encoder_equivalent_luma_downscale_to_sample(const sf::unique_gst_sample_ptr &sample,
                                                                         const double scale_factor) {
        if (!sample || scale_factor <= 0.0 || scale_factor > 1.0) {
            return false;
        }

        auto *buffer = gst_sample_get_buffer(sample.get());
        const auto *caps = gst_sample_get_caps(sample.get());
        if (!buffer || !caps) {
            return false;
        }

        GstVideoInfo info;
        gst_video_info_init(&info);
        if (!gst_video_info_from_caps(&info, caps)) {
            return false;
        }

        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READWRITE)) {
            return false;
        }

        auto *y_plane = static_cast<guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
        if (!y_plane) {
            gst_video_frame_unmap(&frame);
            return false;
        }

        const gint luma_height = GST_VIDEO_INFO_COMP_HEIGHT(&info, 0);
        const gint luma_width = GST_VIDEO_INFO_COMP_WIDTH(&info, 0);
        const gint luma_stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
        const gint luma_pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&info, 0);
        for (gint row = 0; row < luma_height; ++row) {
            auto *row_ptr = y_plane + static_cast<gsize>(row) * static_cast<gsize>(luma_stride);
            for (gint col = 0; col < luma_width; ++col) {
                const gsize index = static_cast<gsize>(col) * static_cast<gsize>(luma_pixel_stride);
                const auto original = static_cast<double>(row_ptr[index]);
                const auto scaled = static_cast<int>(std::floor(original * scale_factor));
                row_ptr[index] = static_cast<guint8>(std::clamp(scaled, 0, 255));
            }
        }

        gst_video_frame_unmap(&frame);
        return true;
    }
} // namespace

TEST_F(decoded_frame_pipeline_test, extract_video_properties_populates_dimensions_fps_and_format) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    auto caps = make_raw_caps("NV12", 1280, 720, 30, 1);
    ASSERT_NE(caps, nullptr);
    sf::unique_gst_caps_no_delete_ptr caps_wrapper(const_cast<GstCaps *>(caps.get()));

    extract_video_properties(*pipeline, caps_wrapper);

    EXPECT_EQ(pipeline->stats_context.statistics.width, 1280);
    EXPECT_EQ(pipeline->stats_context.statistics.height, 720);
    EXPECT_EQ(pipeline->stats_context.statistics.framerate_numerator, 30);
    EXPECT_EQ(pipeline->stats_context.statistics.framerate_denominator, 1);
    ASSERT_NE(pipeline->stats_context.statistics.format, nullptr);
    EXPECT_STREQ(pipeline->stats_context.statistics.format, "NV12");
}

TEST_F(decoded_frame_pipeline_test, extract_video_properties_preserves_existing_fps_when_caps_fps_is_invalid) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    pipeline->stats_context.statistics.framerate_numerator = 25;
    pipeline->stats_context.statistics.framerate_denominator = 1;

    auto caps = make_raw_caps("I420", 640, 360, 0, 1);
    ASSERT_NE(caps, nullptr);
    sf::unique_gst_caps_no_delete_ptr caps_wrapper(const_cast<GstCaps *>(caps.get()));

    extract_video_properties(*pipeline, caps_wrapper);

    EXPECT_EQ(pipeline->stats_context.statistics.width, 640);
    EXPECT_EQ(pipeline->stats_context.statistics.height, 360);
    EXPECT_EQ(pipeline->stats_context.statistics.framerate_numerator, 25);
    EXPECT_EQ(pipeline->stats_context.statistics.framerate_denominator, 1);
    ASSERT_NE(pipeline->stats_context.statistics.format, nullptr);
    EXPECT_STREQ(pipeline->stats_context.statistics.format, "I420");
}

TEST_F(decoded_frame_pipeline_test, create_simplified_caps_keeps_core_fields_metadata_and_nvmm_memory_feature) {
    auto original_caps = make_raw_caps("NV12", 1920, 1080, 60, 1);
    ASSERT_NE(original_caps, nullptr);
    add_test_video_metadata(original_caps.get());
    gst_caps_set_features(original_caps.get(), 0, gst_caps_features_new("memory:NVMM", nullptr));
    sf::unique_gst_caps_no_delete_ptr caps_wrapper(const_cast<GstCaps *>(original_caps.get()));

    const auto simplified_caps = create_simplified_caps(caps_wrapper);
    ASSERT_NE(simplified_caps, nullptr);

    const GstStructure *structure = gst_caps_get_structure(simplified_caps.get(), 0);
    ASSERT_NE(structure, nullptr);

    gint width = 0;
    gint height = 0;
    gint fps_num = 0;
    gint fps_den = 1;
    ASSERT_TRUE(gst_structure_get_int(structure, "width", &width));
    ASSERT_TRUE(gst_structure_get_int(structure, "height", &height));
    ASSERT_TRUE(gst_structure_get_fraction(structure, "framerate", &fps_num, &fps_den));
    EXPECT_EQ(width, 1920);
    EXPECT_EQ(height, 1080);
    EXPECT_EQ(fps_num, 60);
    EXPECT_EQ(fps_den, 1);

    const gchar *format = gst_structure_get_string(structure, "format");
    ASSERT_NE(format, nullptr);
    EXPECT_STREQ(format, "NV12");
    expect_test_video_metadata(structure);

    const GstCapsFeatures *features = gst_caps_get_features(simplified_caps.get(), 0);
    ASSERT_NE(features, nullptr);
    EXPECT_FALSE(gst_caps_features_contains(features, "memory:NVMM"));
}

TEST_F(decoded_frame_pipeline_test, is_cuda_memory_caps_detects_cuda_nvbuf_nvmm_and_non_cuda_caps) {
    EXPECT_FALSE(is_cuda_memory_caps(nullptr));

    auto cuda_caps = make_raw_caps("I420", 16, 16, 30, 1);
    ASSERT_NE(cuda_caps, nullptr);
    gst_structure_set(gst_caps_get_structure(cuda_caps.get(), 0), "memory", G_TYPE_STRING, "CUDAMemory", nullptr);
    EXPECT_TRUE(is_cuda_memory_caps(cuda_caps.get()));

    auto nvbuf_caps = make_raw_caps("I420", 16, 16, 30, 1);
    ASSERT_NE(nvbuf_caps, nullptr);
    add_test_nvbuf_cuda_device_memory(nvbuf_caps.get());
    EXPECT_TRUE(is_cuda_memory_caps(nvbuf_caps.get()));

    auto nvmm_caps = make_raw_caps("I420", 16, 16, 30, 1);
    ASSERT_NE(nvmm_caps, nullptr);
    add_test_nvmm_memory_feature(nvmm_caps.get());
    EXPECT_TRUE(is_cuda_memory_caps(nvmm_caps.get()));

    auto generic_caps = make_raw_caps("I420", 16, 16, 30, 1);
    ASSERT_NE(generic_caps, nullptr);
    gst_structure_set(gst_caps_get_structure(generic_caps.get(), 0), "memory", G_TYPE_STRING, "SystemMemory", nullptr);
    EXPECT_FALSE(is_cuda_memory_caps(generic_caps.get()));
}

TEST_F(decoded_frame_pipeline_test, copy_cuda_buffer_to_system_memory_validates_inputs_and_preserves_payload) {
    sf::unique_gst_buffer_no_delete_ptr null_buffer;
    sf::unique_gst_caps_no_delete_ptr null_caps;
    EXPECT_EQ(copy_cuda_buffer_to_system_memory(null_buffer, null_caps), nullptr);

    auto sample = make_i420_sample(8, 8, true);
    ASSERT_NE(sample, nullptr);
    auto *buffer = gst_sample_get_buffer(sample.get());
    const GstCaps *caps = gst_sample_get_caps(sample.get());
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(caps, nullptr);

    GST_BUFFER_PTS(buffer) = 1234;
    GST_BUFFER_DTS(buffer) = 5678;
    GST_BUFFER_DURATION(buffer) = 999;
    GST_BUFFER_OFFSET(buffer) = 42;
    GST_BUFFER_OFFSET_END(buffer) = 84;

    sf::unique_gst_buffer_no_delete_ptr input_buffer(buffer);
    sf::unique_gst_caps_no_delete_ptr input_caps(const_cast<GstCaps *>(caps));
    const auto copied = copy_cuda_buffer_to_system_memory(input_buffer, input_caps);
    ASSERT_NE(copied, nullptr);

    GstMapInfo source_map;
    GstMapInfo copied_map;
    ASSERT_TRUE(gst_buffer_map(buffer, &source_map, GST_MAP_READ));
    ASSERT_TRUE(gst_buffer_map(copied.get(), &copied_map, GST_MAP_READ));
    ASSERT_EQ(source_map.size, copied_map.size);
    EXPECT_EQ(std::memcmp(source_map.data, copied_map.data, source_map.size), 0);
    gst_buffer_unmap(buffer, &source_map);
    gst_buffer_unmap(copied.get(), &copied_map);

    EXPECT_EQ(GST_BUFFER_PTS(copied.get()), GST_BUFFER_PTS(buffer));
    EXPECT_EQ(GST_BUFFER_DTS(copied.get()), GST_BUFFER_DTS(buffer));
    EXPECT_EQ(GST_BUFFER_DURATION(copied.get()), GST_BUFFER_DURATION(buffer));
    EXPECT_EQ(GST_BUFFER_OFFSET(copied.get()), GST_BUFFER_OFFSET(buffer));
    EXPECT_EQ(GST_BUFFER_OFFSET_END(copied.get()), GST_BUFFER_OFFSET_END(buffer));
}

TEST_F(decoded_frame_pipeline_test, convert_cuda_sample_to_system_memory_preserves_video_metadata) {
    auto sample = make_i420_sample(8, 8, true);
    ASSERT_NE(sample, nullptr);

    GstCaps *input_caps = const_cast<GstCaps *>(gst_sample_get_caps(sample.get()));
    ASSERT_NE(input_caps, nullptr);
    add_test_video_metadata(input_caps);
    add_test_nvbuf_cuda_device_memory(input_caps);
    add_test_nvmm_memory_feature(input_caps);

    const auto converted = convert_cuda_sample_to_system_memory(sample, 5, 5004);
    ASSERT_NE(converted, nullptr);
    EXPECT_NE(gst_sample_get_buffer(converted.get()), gst_sample_get_buffer(sample.get()));

    const GstCaps *converted_caps = gst_sample_get_caps(converted.get());
    ASSERT_NE(converted_caps, nullptr);
    const GstStructure *structure = gst_caps_get_structure(converted_caps, 0);
    expect_test_video_metadata(structure);
    expect_no_gpu_memory_metadata(converted_caps);
}

TEST_F(decoded_frame_pipeline_test, ensure_system_memory_sample_converts_nvbuf_backed_sample) {
    auto sample = make_i420_sample(8, 8, false);
    ASSERT_NE(sample, nullptr);

    GstCaps *input_caps = const_cast<GstCaps *>(gst_sample_get_caps(sample.get()));
    ASSERT_NE(input_caps, nullptr);
    add_test_video_metadata(input_caps);
    add_test_nvbuf_cuda_device_memory(input_caps);
    add_test_nvmm_memory_feature(input_caps);
    ASSERT_TRUE(is_cuda_memory_caps(input_caps));

    auto *original_buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(original_buffer, nullptr);

    const auto converted = ensure_system_memory_sample(std::move(sample), 6, 5004);
    ASSERT_NE(converted, nullptr);
    EXPECT_NE(gst_sample_get_buffer(converted.get()), original_buffer);
    expect_no_gpu_memory_metadata(gst_sample_get_caps(converted.get()));
    expect_test_video_metadata(gst_caps_get_structure(gst_sample_get_caps(converted.get()), 0));
}

TEST_F(decoded_frame_pipeline_test, ensure_system_memory_sample_and_prepare_buffer_cover_non_cuda_paths) {
    auto sample = make_i420_sample(8, 8, false);
    ASSERT_NE(sample, nullptr);
    auto *original_buffer = gst_sample_get_buffer(sample.get());
    ASSERT_NE(original_buffer, nullptr);

    auto ensured = ensure_system_memory_sample(std::move(sample), 1, 5004);
    ASSERT_NE(ensured, nullptr);
    EXPECT_EQ(gst_sample_get_buffer(ensured.get()), original_buffer);

    const auto push_buffer = prepare_buffer_for_push(ensured, 1, 5004);
    ASSERT_NE(push_buffer, nullptr);
    EXPECT_EQ(gst_buffer_get_size(push_buffer.get()), gst_buffer_get_size(original_buffer));
}

TEST_F(decoded_frame_pipeline_test, prepare_buffer_for_push_fails_when_sample_has_no_buffer) {
    auto caps = make_raw_caps("I420", 8, 8, 30, 1);
    ASSERT_NE(caps, nullptr);
    auto sample = sf::unique_gst_sample_ptr(gst_sample_new(nullptr, caps.get(), nullptr, nullptr));
    ASSERT_NE(sample, nullptr);

    EXPECT_EQ(prepare_buffer_for_push(sample, 2, 5005), nullptr);
}

TEST_F(decoded_frame_pipeline_test, luma_backscale_raises_luma_as_expected) {
    auto sample = make_constant_i420_sample(4, 4, 120, 128, 128);
    ASSERT_NE(sample, nullptr);
    auto *buffer = gst_sample_get_buffer(sample.get());
    const auto *caps = gst_sample_get_caps(sample.get());
    ASSERT_NE(buffer, nullptr);
    ASSERT_NE(caps, nullptr);
    const auto mean_before_backscale = compute_i420_luma_mean(sample);
    ASSERT_TRUE(mean_before_backscale.has_value());
    EXPECT_NEAR(*mean_before_backscale, 120.0, 0.01);

    constexpr int ratio_percentage = 50;
    constexpr int backscale_ratio_value = 100;
    ASSERT_TRUE(apply_luma_backscaling_to_sample(sample, 1, 5004, ratio_percentage, backscale_ratio_value));

    GstVideoInfo info;
    gst_video_info_init(&info);
    ASSERT_TRUE(gst_video_info_from_caps(&info, caps));

    GstVideoFrame after_backscale_frame;
    ASSERT_TRUE(gst_video_frame_map(&after_backscale_frame, &info, buffer, GST_MAP_READ));
    const auto *y_plane_after_backscale =
            static_cast<const guint8 *>(GST_VIDEO_FRAME_PLANE_DATA(&after_backscale_frame, 0));
    ASSERT_NE(y_plane_after_backscale, nullptr);
    EXPECT_EQ(y_plane_after_backscale[0], 240);
    gst_video_frame_unmap(&after_backscale_frame);
    const auto mean_after_backscale = compute_i420_luma_mean(sample);
    ASSERT_TRUE(mean_after_backscale.has_value());
    EXPECT_NEAR(*mean_after_backscale, 240.0, 0.01);
}

TEST_F(decoded_frame_pipeline_test, tiny_frame_luma_stages_match_expected_pixels_and_means) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    const std::vector<guint8> original_luma = {16, 64, 128, 250};
    auto sample = make_pattern_i420_sample(2, 2, original_luma, 128, 128);
    ASSERT_NE(sample, nullptr);
    GstCaps *sample_caps = const_cast<GstCaps *>(gst_sample_get_caps(sample.get()));
    ASSERT_NE(sample_caps, nullptr);
    add_test_video_metadata(sample_caps);
    expect_test_video_metadata(gst_caps_get_structure(sample_caps, 0));

    const auto initial_luma = read_i420_luma_values(sample);
    ASSERT_TRUE(initial_luma.has_value());
    EXPECT_EQ(*initial_luma, original_luma);

    const auto initial_mean = compute_i420_luma_mean(sample);
    ASSERT_TRUE(initial_mean.has_value());
    EXPECT_NEAR(*initial_mean, 114.5, 0.01);

    constexpr double downscale_factor = 0.5;
    ASSERT_TRUE(apply_encoder_equivalent_luma_downscale_to_sample(sample, downscale_factor));

    const std::vector<guint8> expected_downscaled_luma = {8, 32, 64, 125};
    const auto downscaled_luma = read_i420_luma_values(sample);
    ASSERT_TRUE(downscaled_luma.has_value());
    EXPECT_EQ(*downscaled_luma, expected_downscaled_luma);

    const auto downscaled_mean = compute_i420_luma_mean(sample);
    ASSERT_TRUE(downscaled_mean.has_value());
    EXPECT_NEAR(*downscaled_mean, 57.25, 0.01);

    const auto signal = sf::derive_luma_adaptation_signal(downscale_factor);
    sf::encoder_optimization_info eoi{};
    eoi.cancel_flag = false;
    eoi.type = sf::encoder_optimization_info::type_luma_adaptation;
    eoi.luma_adaptation_idc = 1U;
    eoi.ratio_luma_sign_flag = signal.ratio_luma_sign_flag;
    eoi.ratio_luma_value = signal.ratio_luma_value;
    eoi.backscale_ratio_value = signal.backscale_ratio_value;
    const std::optional<sf::encoder_optimization_info> eoi_metadata = eoi;

    const int ratio_percentage = sf::derive_luma_ratio_percentage(signal.ratio_luma_sign_flag, signal.ratio_luma_value);
    ASSERT_GT(ratio_percentage, 0);
    ASSERT_TRUE(eoi_metadata.has_value());
    ASSERT_TRUE(eoi_metadata->backscale_ratio_value.has_value());
    ASSERT_TRUE(apply_luma_backscaling_to_sample(sample, 1, 5004, ratio_percentage,
                                                 static_cast<int>(eoi_metadata->backscale_ratio_value.value())));

    const auto backscaled_luma = read_i420_luma_values(sample);
    ASSERT_TRUE(backscaled_luma.has_value());
    EXPECT_EQ(*backscaled_luma, original_luma);

    const auto backscaled_mean = compute_i420_luma_mean(sample);
    ASSERT_TRUE(backscaled_mean.has_value());
    EXPECT_NEAR(*backscaled_mean, *initial_mean, 0.01);
    EXPECT_GT(*backscaled_mean, *downscaled_mean);
    expect_test_video_metadata(gst_caps_get_structure(gst_sample_get_caps(sample.get()), 0));
}
