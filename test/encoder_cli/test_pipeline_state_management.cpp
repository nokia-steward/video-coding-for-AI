// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/pipeline/state_management.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

namespace {
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

class PipelineStateManagementTest : public GStreamerTestFixture {};

TEST_F(PipelineStateManagementTest, ReturnsTrueForInputPipelineAsyncOrSuccessTransition) {
    auto pipeline = make_minimal_pipeline("test-input-pipeline");
    if (!pipeline) {
        GTEST_SKIP() << "Required GStreamer test elements are unavailable.";
    }

    const bool started = start_pipeline_with_error_check(pipeline.get(), "input pipeline");
    EXPECT_TRUE(started);
}

TEST_F(PipelineStateManagementTest, ReturnsFalseWhenStateChangeFailsToInitiate) {
    const bool started = start_pipeline_with_error_check(nullptr, "output pipeline");
    EXPECT_FALSE(started);
}

TEST_F(PipelineStateManagementTest, ReturnsTrueForOutputPipelineWhenTransitionSucceeds) {
    auto pipeline = make_minimal_pipeline("test-output-pipeline");
    if (!pipeline) {
        GTEST_SKIP() << "Required GStreamer test elements are unavailable.";
    }

    const bool started = start_pipeline_with_error_check(pipeline.get(), "output pipeline");
    EXPECT_TRUE(started);
}
