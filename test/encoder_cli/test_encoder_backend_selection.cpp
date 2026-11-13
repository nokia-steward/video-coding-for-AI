// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>
#include <string>

#include "../../encoder_cli/src/bridge/orchestration/encoded_bridge_startup_contract.h"
#include "../../encoder_cli/src/encoder_backend/encoder_backend.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/frame_path_meta.h"
#include "../../encoder_cli/src/processing/frame_processing_utils.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_i420_config() {
        encoder_config config;
        config.width = 4;
        config.height = 4;
        config.format = sf::video_format::i420;
        config.enable_luma_adaptation = true;
        config.luma_downscale_factor = 0.5;
        config.input_type_value = input_type::udp; // exercise the synchronous push path
        return config;
    }

    [[nodiscard]] sf::unique_gst_caps_ptr make_i420_caps(const int width, const int height) {
        return sf::unique_gst_caps_ptr(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", "width",
                                                           G_TYPE_INT, width, "height", G_TYPE_INT, height, "framerate",
                                                           GST_TYPE_FRACTION, 30, 1, nullptr));
    }

    [[nodiscard]] sf::unique_gst_buffer_ptr make_i420_buffer(const int width, const int height, const uint8_t y_value,
                                                             const uint8_t chroma_u_value, const uint8_t v_value) {
        GstVideoInfo info;
        gst_video_info_init(&info);
        if (!gst_video_info_set_format(&info, GST_VIDEO_FORMAT_I420, width, height)) {
            return nullptr;
        }

        auto buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, info.size, nullptr));
        if (!buffer) {
            return nullptr;
        }

        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &info, buffer.get(), GST_MAP_WRITE)) {
            return nullptr;
        }

        for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES(&info); ++plane) {
            const gint plane_height = GST_VIDEO_INFO_COMP_HEIGHT(&info, plane);
            const gint plane_width = GST_VIDEO_INFO_COMP_WIDTH(&info, plane);
            const gint plane_stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, plane);
            const gint pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE(&info, plane);
            auto *plane_data = static_cast<uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, plane));
            const uint8_t fill_value = plane == 0 ? y_value : (plane == 1 ? chroma_u_value : v_value);
            for (gint row = 0; row < plane_height; ++row) {
                auto *row_ptr = plane_data + static_cast<gsize>(row) * static_cast<gsize>(plane_stride);
                for (gint col = 0; col < plane_width; ++col) {
                    row_ptr[static_cast<gsize>(col) * static_cast<gsize>(pixel_stride)] = fill_value;
                }
            }
        }

        gst_video_frame_unmap(&frame);
        return buffer;
    }

    [[nodiscard]] uint8_t read_first_luma(const sf::unique_gst_sample_ptr &sample, const int width, const int height) {
        if (!sample) {
            return 0;
        }

        GstVideoInfo info;
        gst_video_info_init(&info);
        if (!gst_video_info_set_format(&info, GST_VIDEO_FORMAT_I420, width, height)) {
            return 0;
        }

        auto *buffer = gst_sample_get_buffer(sample.get());
        if (!buffer) {
            return 0;
        }

        GstVideoFrame frame;
        if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
            return 0;
        }

        const auto *y_plane = static_cast<const uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
        const gint stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
        const uint8_t first_luma = y_plane != nullptr ? y_plane[stride * 0] : 0;
        gst_video_frame_unmap(&frame);
        return first_luma;
    }

    [[nodiscard]] bool attach_test_contract(sf::unique_gst_buffer_ptr &buffer, const uint64_t sequence_id) {
        return attach_frame_contract_metadata_to_buffer(buffer.get(), frame_path_type::inference, sequence_id);
    }
} // namespace

class EncoderBackendSelectionGstFixture : public GStreamerTestFixture {};

TEST(EncoderBackendSelection, NvencApiBackendNotSelectedWhenQpMapNotRequested) {
    encoder_config config;
    config.background_mode_value = background_mode::off;
    config.effective_background_mode_value = background_mode::off;
    global_data data(std::move(config));

    const auto result = try_create_nvenc_api_backend(data.params);
    EXPECT_EQ(result.backend, nullptr);
    EXPECT_FALSE(result.qp_map_direct_supported);
    EXPECT_FALSE(result.reason.empty());
}

TEST(EncoderBackendSelection, NvencApiBackendReportsReasonWhenQpMapRequested) {
    encoder_config config;
    config.background_mode_value = background_mode::qp_map_delta;
    config.effective_background_mode_value = background_mode::qp_map_delta;
    global_data data(std::move(config));

    const auto result = try_create_nvenc_api_backend(data.params);
    EXPECT_EQ(result.backend, nullptr);
    EXPECT_FALSE(result.qp_map_direct_supported);
    EXPECT_FALSE(result.reason.empty());

#if !defined(HAVE_NVENCODEAPI_HEADERS) || (HAVE_NVENCODEAPI_HEADERS == 0)
    EXPECT_NE(result.reason.find("headers"), std::string::npos);
#endif
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadAcceptsExpectedGridSizeForDirectBackend) {
    qp_map_frame_payload payload{};
    payload.width = 128;
    payload.height = 64;
    payload.ctb_size = 32;
    payload.delta_map = {10, 10, 10, 10, 10, 10, 10, 10};

    std::string reason;
    EXPECT_TRUE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_TRUE(reason.empty());
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadRejectsMalformedGrid) {
    qp_map_frame_payload payload{};
    payload.width = 192;
    payload.height = 128;
    payload.ctb_size = 32; // ceil(192/32)*ceil(128/32) = 24 cells; delta map has 2
    payload.delta_map = {10, 10};

    std::string reason;
    EXPECT_FALSE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_NE(reason.find("size mismatch"), std::string::npos);
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadRejectsNonPositiveDimensions) {
    qp_map_frame_payload payload{};
    payload.width = 0;
    payload.height = 64;
    payload.ctb_size = 32;
    payload.delta_map = {10, 10};

    std::string reason;
    EXPECT_FALSE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_NE(reason.find("invalid frame dimensions"), std::string::npos);
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadRejectsInvalidCtbSize) {
    qp_map_frame_payload payload{};
    payload.width = 128;
    payload.height = 64;
    payload.ctb_size = 0;
    payload.delta_map = {10, 10};

    std::string reason;
    EXPECT_FALSE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_NE(reason.find("invalid CTB size"), std::string::npos);
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadRejectsUnsupportedDirectBackendCtbSize) {
    qp_map_frame_payload payload{};
    payload.width = 128;
    payload.height = 64;
    payload.ctb_size = 64;
    payload.delta_map = {10, 10};

    std::string reason;
    EXPECT_FALSE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_NE(reason.find("expects CTB size 32"), std::string::npos);
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadRejectsEmptyDeltaMap) {
    qp_map_frame_payload payload{};
    payload.width = 128;
    payload.height = 64;
    payload.ctb_size = 32;

    std::string reason;
    EXPECT_FALSE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_NE(reason.find("delta map is empty"), std::string::npos);
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadAcceptsCeilDivisionGridSizing) {
    qp_map_frame_payload payload{};
    payload.width = 65;
    payload.height = 33;
    payload.ctb_size = 32;
    payload.delta_map = {1, 2, 3, 4, 5, 6}; // ceil(65/32)=3, ceil(33/32)=2 -> 6 cells

    std::string reason;
    EXPECT_TRUE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_TRUE(reason.empty());
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadSupportsNullReasonPointer) {
    qp_map_frame_payload payload{};
    payload.width = 192;
    payload.height = 128;
    payload.ctb_size = 32;
    payload.delta_map = {10, 10};

    EXPECT_FALSE(validate_qp_map_frame_payload(payload, nullptr));
}

TEST(EncoderBackendSelection, ValidateQpMapPayloadClearsReasonAfterSuccessfulValidation) {
    qp_map_frame_payload invalid_payload{};
    invalid_payload.width = 128;
    invalid_payload.height = 64;
    invalid_payload.ctb_size = 0;
    invalid_payload.delta_map = {1, 2};

    std::string reason = "stale";
    EXPECT_FALSE(validate_qp_map_frame_payload(invalid_payload, &reason));
    EXPECT_FALSE(reason.empty());

    qp_map_frame_payload valid_payload{};
    valid_payload.width = 65;
    valid_payload.height = 33;
    valid_payload.ctb_size = 32;
    valid_payload.delta_map = {1, 2, 3, 4, 5, 6};

    EXPECT_TRUE(validate_qp_map_frame_payload(valid_payload, &reason));
    EXPECT_TRUE(reason.empty());
}

TEST(EncoderBackendSelection, is_x86_architecture_and_expected_qp_map_elements_behavior_is_reflected_by_validation) {
    qp_map_frame_payload payload{};
    payload.width = 129;
    payload.height = 65;
    payload.ctb_size = 32;
    payload.delta_map = {1, 2, 3, 4, 5, 6, 7, 8, 9}; // ceil(129/32)=5, ceil(65/32)=3 -> 15 expected, invalid

    std::string reason;
    EXPECT_FALSE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_FALSE(reason.empty());
}

TEST(EncoderBackendSelection, validate_qp_map_payload_rejects_out_of_range_delta_values) {
    qp_map_frame_payload payload{};
    payload.width = 64;
    payload.height = 64;
    payload.ctb_size = 32;
    payload.delta_map = {0, 0, 0, -1};

    std::string reason;
    EXPECT_FALSE(validate_qp_map_frame_payload(payload, &reason));
    EXPECT_FALSE(reason.empty());
}

TEST(EncoderBackendSelection, CreateGstBackendExposesExpectedKindAndPipelineRequirement) {
    const auto backend = create_gst_encoder_backend();
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->kind(), encoder_backend_kind::gst_pipeline);
    EXPECT_TRUE(backend->requires_gst_encoding_pipeline());
}

TEST(EncoderBackendSelection, GstBackendRejectsNullOutputBuffer) {
    encoder_config config;
    global_data data(std::move(config));

    auto backend = create_gst_encoder_backend();
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->submit_frame(data, sf::unique_gst_buffer_ptr(nullptr), std::nullopt));
}

TEST_F(EncoderBackendSelectionGstFixture, GstBackendSubmitFrameAppliesLumaScalingWhenEnabled) {
    auto config = make_base_i420_config();
    global_data data(std::move(config));

    auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("gst-backend-luma-scaling-pipeline"));
    ASSERT_NE(pipeline, nullptr);
    auto *appsrc = gst_element_factory_make("appsrc", "gst-backend-luma-scaling-src");
    auto *appsink = gst_element_factory_make("appsink", "gst-backend-luma-scaling-sink");
    ASSERT_NE(appsrc, nullptr);
    ASSERT_NE(appsink, nullptr);

    const auto caps = make_i420_caps(4, 4);
    ASSERT_NE(caps, nullptr);
    g_object_set(appsrc, "caps", caps.get(), "format", GST_FORMAT_TIME, "is-live", false, "do-timestamp", false,
                 nullptr);
    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, appsink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, appsink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, 2 * GST_SECOND);

    data.encoded_ports.encoding_src = appsrc;
    ASSERT_TRUE(configure_encoded_bridge_startup_contract(data.encoded_ports, data.egress_bridge_runtime,
                                                          encoded_bridge_target::encoding_src, caps.get(),
                                                          encoded_bridge_startup_contract_options{}));
    auto backend = create_gst_encoder_backend();
    ASSERT_NE(backend, nullptr);

    auto buffer = make_i420_buffer(4, 4, 200, 123, 123);
    ASSERT_NE(buffer, nullptr);
    ASSERT_TRUE(attach_test_contract(buffer, 1));
    EXPECT_TRUE(backend->submit_frame(data, std::move(buffer), std::nullopt));

    const auto pending_state = data.pop_pending_encoded_contract_state();
    ASSERT_TRUE(pending_state.has_value());
    EXPECT_EQ(pending_state->sequence_id, 1U);
    EXPECT_TRUE(pending_state->luma_adaptation.enabled);
    EXPECT_DOUBLE_EQ(pending_state->luma_adaptation.downscale_factor, 0.5);

    auto output_sample = sf::unique_gst_sample_ptr(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND));
    ASSERT_NE(output_sample, nullptr);
    EXPECT_EQ(read_first_luma(output_sample, 4, 4), 100);

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}

TEST_F(EncoderBackendSelectionGstFixture, GstBackendSubmitFrameFailOpenKeepsOriginalLumaOnScalingFailure) {
    auto config = make_base_i420_config();
    config.luma_downscale_factor = 1.5; // Deliberately invalid for pixel-domain scaling.
    global_data data(std::move(config));

    auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("gst-backend-luma-failopen-pipeline"));
    ASSERT_NE(pipeline, nullptr);
    auto *appsrc = gst_element_factory_make("appsrc", "gst-backend-luma-failopen-src");
    auto *appsink = gst_element_factory_make("appsink", "gst-backend-luma-failopen-sink");
    ASSERT_NE(appsrc, nullptr);
    ASSERT_NE(appsink, nullptr);

    const auto caps = make_i420_caps(4, 4);
    ASSERT_NE(caps, nullptr);
    g_object_set(appsrc, "caps", caps.get(), "format", GST_FORMAT_TIME, "is-live", false, "do-timestamp", false,
                 nullptr);
    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(pipeline.get()), appsrc, appsink, nullptr);
    ASSERT_TRUE(gst_element_link(appsrc, appsink));
    ASSERT_NE(gst_element_set_state(pipeline.get(), GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);
    gst_element_get_state(pipeline.get(), nullptr, nullptr, 2 * GST_SECOND);

    data.encoded_ports.encoding_src = appsrc;
    ASSERT_TRUE(configure_encoded_bridge_startup_contract(data.encoded_ports, data.egress_bridge_runtime,
                                                          encoded_bridge_target::encoding_src, caps.get(),
                                                          encoded_bridge_startup_contract_options{}));
    auto backend = create_gst_encoder_backend();
    ASSERT_NE(backend, nullptr);

    auto buffer = make_i420_buffer(4, 4, 200, 123, 123);
    ASSERT_NE(buffer, nullptr);
    ASSERT_TRUE(attach_test_contract(buffer, 1));
    EXPECT_TRUE(backend->submit_frame(data, std::move(buffer), std::nullopt));

    const auto pending_state = data.pop_pending_encoded_contract_state();
    ASSERT_TRUE(pending_state.has_value());
    EXPECT_EQ(pending_state->sequence_id, 1U);
    EXPECT_FALSE(pending_state->luma_adaptation.enabled);
    EXPECT_DOUBLE_EQ(pending_state->luma_adaptation.downscale_factor, 1.0);

    auto output_sample = sf::unique_gst_sample_ptr(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND));
    ASSERT_NE(output_sample, nullptr);
    EXPECT_EQ(read_first_luma(output_sample, 4, 4), 200);

    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
}

TEST(EncoderBackendSelection, DirectBackendLumaAdaptationStateIsEnabledWhenScalingSucceeds) {
    auto buffer = make_i420_buffer(4, 4, 200, 123, 123);
    ASSERT_NE(buffer, nullptr);

    const frame_properties props = {
            .width = 4,
            .height = 4,
            .format = "I420",
    };
    const auto luma_state = maybe_apply_luma_adaptation_to_buffer(buffer, props, true, 0.5);

    EXPECT_TRUE(luma_state.enabled);
    EXPECT_DOUBLE_EQ(luma_state.downscale_factor, 0.5);

    const auto caps = make_i420_caps(4, 4);
    ASSERT_NE(caps, nullptr);
    auto sample = sf::unique_gst_sample_ptr(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    ASSERT_NE(sample, nullptr);
    EXPECT_EQ(read_first_luma(sample, 4, 4), 100);
}

TEST(EncoderBackendSelection, DirectBackendLumaAdaptationStateStaysDisabledWhenScalingFails) {
    auto buffer = make_i420_buffer(4, 4, 200, 123, 123);
    ASSERT_NE(buffer, nullptr);

    const frame_properties props = {
            .width = 4,
            .height = 4,
            .format = "I420",
    };
    const auto luma_state = maybe_apply_luma_adaptation_to_buffer(buffer, props, true, 1.5);

    EXPECT_FALSE(luma_state.enabled);
    EXPECT_DOUBLE_EQ(luma_state.downscale_factor, 1.0);

    const auto caps = make_i420_caps(4, 4);
    ASSERT_NE(caps, nullptr);
    auto sample = sf::unique_gst_sample_ptr(gst_sample_new(buffer.get(), caps.get(), nullptr, nullptr));
    ASSERT_NE(sample, nullptr);
    EXPECT_EQ(read_first_luma(sample, 4, 4), 200);
}
