// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../common/gst_test_fixture.h"

#ifdef HAVE_DEEPSTREAM
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/input_ingress_facade.h"
#include "../../encoder_cli/src/gst/input_types/roi_input.h"

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

    void cleanup_deepstream_elements(deepstream_elements &elements) {
        if (elements.streammux)
            gst_object_unref(elements.streammux);
        if (elements.pgie)
            gst_object_unref(elements.pgie);
        if (elements.tracker)
            gst_object_unref(elements.tracker);
        if (elements.nvvidconv)
            gst_object_unref(elements.nvvidconv);
        if (elements.nvvidconv2)
            gst_object_unref(elements.nvvidconv2);
        if (elements.nvvidconv_pre_inference)
            gst_object_unref(elements.nvvidconv_pre_inference);
        if (elements.capsfilter_inference)
            gst_object_unref(elements.capsfilter_inference);
        if (elements.videoconvert_inference)
            gst_object_unref(elements.videoconvert_inference);
        if (elements.appsink)
            gst_object_unref(elements.appsink);
        if (elements.valve_inference)
            gst_object_unref(elements.valve_inference);
        if (elements.bypass.valve_bypass)
            gst_object_unref(elements.bypass.valve_bypass);
        if (elements.bypass.queue_bypass)
            gst_object_unref(elements.bypass.queue_bypass);
        if (elements.bypass.nvvidconv_bypass)
            gst_object_unref(elements.bypass.nvvidconv_bypass);
        if (elements.bypass.capsfilter_bypass)
            gst_object_unref(elements.bypass.capsfilter_bypass);
        if (elements.bypass.videoconvert_bypass)
            gst_object_unref(elements.bypass.videoconvert_bypass);
        if (elements.bypass.appsink_bypass)
            gst_object_unref(elements.bypass.appsink_bypass);
    }
} // namespace

class RoiInputPipelineTest : public GStreamerTestFixture {};

TEST_F(RoiInputPipelineTest, CreateDeepstreamElementsWithoutInferenceLeavesPgieAndTrackerNull) {
    auto config = make_base_config();
    config.inference_engine = false;
    global_data data(std::move(config));

    deepstream_elements elements{};
    const bool created = create_deepstream_elements(elements, data.params);
    if (!created) {
        GTEST_SKIP() << "Required DeepStream plugins are unavailable in this environment";
    }

    EXPECT_EQ(elements.pgie, nullptr);
    EXPECT_EQ(elements.tracker, nullptr);
    cleanup_deepstream_elements(elements);
}

TEST_F(RoiInputPipelineTest, CreateDeepstreamElementsWithInferenceAndTrackerDisabledSkipsTrackerElement) {
    auto config = make_base_config();
    config.inference_engine = true;
    config.detection.enable_tracker = false;
    global_data data(std::move(config));

    deepstream_elements elements{};
    const bool created = create_deepstream_elements(elements, data.params);
    if (!created) {
        GTEST_SKIP() << "Required DeepStream plugins are unavailable in this environment";
    }

    EXPECT_NE(elements.pgie, nullptr);
    EXPECT_EQ(elements.tracker, nullptr);
    cleanup_deepstream_elements(elements);
}

TEST_F(RoiInputPipelineTest, SetupPtsPreservationProbesFailsWhenValveHasNoSrcPad) {
    auto config = make_base_config();
    global_data data(std::move(config));

    deepstream_elements elements{};
    elements.valve_inference = gst_element_factory_make("fakesink", "test-valve-no-src");
    elements.videoconvert_inference = gst_element_factory_make("identity", "test-videoconvert-src");
    ASSERT_NE(elements.valve_inference, nullptr);
    ASSERT_NE(elements.videoconvert_inference, nullptr);

    input_ingress_facade view(data);
    EXPECT_FALSE(setup_pts_preservation_probes(view, elements));
    gst_object_unref(elements.valve_inference);
    gst_object_unref(elements.videoconvert_inference);
}

TEST_F(RoiInputPipelineTest, SetupPtsPreservationProbesFailsWhenPostInferencePadMissing) {
    auto config = make_base_config();
    global_data data(std::move(config));

    deepstream_elements elements{};
    elements.valve_inference = gst_element_factory_make("identity", "test-valve-src");
    elements.videoconvert_inference = gst_element_factory_make("fakesink", "test-videoconvert-no-src");
    ASSERT_NE(elements.valve_inference, nullptr);
    ASSERT_NE(elements.videoconvert_inference, nullptr);

    input_ingress_facade view(data);
    EXPECT_FALSE(setup_pts_preservation_probes(view, elements));
    gst_object_unref(elements.valve_inference);
    gst_object_unref(elements.videoconvert_inference);
}

#else
TEST(RoiInputPipelineTest, SkippedWithoutDeepStream) { GTEST_SKIP() << "DeepStream not available"; }
#endif
