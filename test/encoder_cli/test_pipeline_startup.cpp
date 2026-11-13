// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/lifecycle_facade.h"
#include "../../encoder_cli/src/encoder_backend/encoder_backend.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/pipeline/startup.h"
#include "../common/gst_test_fixture.h"

namespace {
    class no_op_encoder_backend final : public encoder_backend {
    public:
        [[nodiscard]] encoder_backend_kind kind() const override { return encoder_backend_kind::gst_pipeline; }

        [[nodiscard]] bool requires_gst_encoding_pipeline() const override { return false; }

        [[nodiscard]] bool submit_frame([[maybe_unused]] global_data &data,
                                        [[maybe_unused]] sf::unique_gst_buffer_ptr output_buffer,
                                        [[maybe_unused]] std::optional<qp_map_frame_payload> qp_map_payload) override {
            return true;
        }

        void signal_eos([[maybe_unused]] global_data &data) override {}
    };

    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 64;
        config.height = 64;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_64x64_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }

    [[nodiscard]] sf::unique_gst_element_ptr make_minimal_pipeline(const char *name) {
        auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new(name));
        if (!pipeline) {
            return {};
        }
        GstElement *source = gst_element_factory_make("fakesrc", nullptr);
        GstElement *sink = gst_element_factory_make("fakesink", nullptr);
        if (!source || !sink) {
            if (source) {
                gst_object_unref(source);
            }
            if (sink) {
                gst_object_unref(sink);
            }
            return {};
        }
        gst_bin_add_many(GST_BIN(pipeline.get()), source, sink, nullptr);
        if (!gst_element_link(source, sink)) {
            return {};
        }
        return pipeline;
    }
} // namespace

class PipelineStartupTest : public GStreamerTestFixture {};

TEST_F(PipelineStartupTest, StartInputAndEncodingReturnsFalseWhenInputPipelineMissing) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);
    lifecycle_facade runtime(data);

    const bool result = start_input_and_encoding_pipelines(runtime);
    EXPECT_FALSE(result);
}

TEST_F(PipelineStartupTest, StartInputAndEncodingReturnsTrueWithValidInputPipelineAndNoGstEncodingBackend) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);
    data.input_pipeline = make_minimal_pipeline("test-input-pipeline");
    if (!data.input_pipeline) {
        GTEST_SKIP() << "Required GStreamer test elements are unavailable.";
    }
    data.active_encoder_backend = std::make_unique<no_op_encoder_backend>();
    lifecycle_facade runtime(data);

    const bool result = start_input_and_encoding_pipelines(runtime);
    EXPECT_TRUE(result);
}

TEST_F(PipelineStartupTest, StartAllPipelinesInWebRtcModeDoesNotStartInputPipelineImmediately) {
    auto config = make_base_config();
    config.communication.transport_type = sf::communication::transport_type::webrtc;
    config.communication.host = "127.0.0.1";
    config.communication.port = 5000;
    config.webrtc.signaling_url = "ws://localhost:8080";
    global_data data(std::move(config));
    data.stop_processing.store(true);

    data.input_pipeline = make_minimal_pipeline("test-input-pipeline");
    data.output_pipeline = make_minimal_pipeline("test-output-pipeline");
    if (!data.input_pipeline || !data.output_pipeline) {
        GTEST_SKIP() << "Required GStreamer test elements are unavailable.";
    }

    lifecycle_facade runtime(data);
    ASSERT_NO_THROW(start_all_pipelines(runtime));

    GstState input_state = GST_STATE_NULL;
    GstState input_pending = GST_STATE_VOID_PENDING;
    const auto get_result = gst_element_get_state(data.input_pipeline.get(), &input_state, &input_pending, 0);
    (void) get_result;
    EXPECT_EQ(input_state, GST_STATE_NULL);
}

TEST_F(PipelineStartupTest, StartInputPipelineOnlyReturnsFalseWhenInputPipelineMissing) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);
    lifecycle_facade runtime(data);

    EXPECT_FALSE(start_input_pipeline_only(runtime));
}

TEST_F(PipelineStartupTest, StartInputPipelineOnlyStartsAValidInputPipeline) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);
    data.input_pipeline = make_minimal_pipeline("test-input-pipeline");
    if (!data.input_pipeline) {
        GTEST_SKIP() << "Required GStreamer test elements are unavailable.";
    }
    lifecycle_facade runtime(data);

    EXPECT_TRUE(start_input_pipeline_only(runtime));
}

TEST_F(PipelineStartupTest, StartAllPipelinesInFileModeDeferInputPipelineUntilWorkersArmed) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.stop_processing.store(true);
    data.active_encoder_backend = std::make_unique<no_op_encoder_backend>();

    data.input_pipeline = make_minimal_pipeline("test-input-pipeline");
    data.encoding_pipeline = make_minimal_pipeline("test-encoding-pipeline");
    data.output_pipeline = make_minimal_pipeline("test-output-pipeline");
    if (!data.input_pipeline || !data.encoding_pipeline || !data.output_pipeline) {
        GTEST_SKIP() << "Required GStreamer test elements are unavailable.";
    }

    lifecycle_facade runtime(data);
    ASSERT_NO_THROW(start_all_pipelines(runtime));

    // The input pipeline must remain unstarted in file mode; the runner starts it later via
    // start_input_pipeline_only() once the consuming workers are armed.
    GstState input_state = GST_STATE_NULL;
    GstState input_pending = GST_STATE_VOID_PENDING;
    const auto get_result = gst_element_get_state(data.input_pipeline.get(), &input_state, &input_pending, 0);
    (void) get_result;
    EXPECT_EQ(input_state, GST_STATE_NULL);
}
