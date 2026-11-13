// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../libvcai/include/gst_raii.h"
#include "../../rtp_server/src/pipeline/output/display/display_branch.h"
#include "../../rtp_server/src/pipeline/output/display/overlay_probes.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    struct overlay_probes_test : GStreamerTestFixtureRtpServer {
        GMainLoop *test_loop = nullptr;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            init_display_branch_debug_category();
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
} // namespace

TEST_F(overlay_probes_test, add_display_overlay_probes_returns_false_for_null_pipeline) {
    EXPECT_FALSE(add_display_overlay_probes(nullptr));
}

TEST_F(overlay_probes_test, add_display_overlay_probes_returns_true_when_overlays_disabled) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    pipeline->draw_fps = false;
    pipeline->draw_bitrate = false;
    pipeline->draw_bitrate_large = false;

    EXPECT_TRUE(add_display_overlay_probes(pipeline.get()));
}

TEST_F(overlay_probes_test, add_display_overlay_probes_returns_false_when_enabled_and_probe_element_missing) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    pipeline->draw_fps = true;
    pipeline->draw_bitrate = false;
    pipeline->draw_bitrate_large = false;

    EXPECT_FALSE(add_display_overlay_probes(pipeline.get()));
}

TEST_F(overlay_probes_test, add_display_overlay_probes_attaches_when_nvosd_probe_element_exists) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    pipeline->draw_fps = true;

    GstElement *probe_element = gst_element_factory_make("identity", "overlay-probe-element");
    ASSERT_NE(probe_element, nullptr);
    pipeline->nvosd_display = sf::unique_gst_element_no_delete_ptr(probe_element);

    EXPECT_TRUE(add_display_overlay_probes(pipeline.get()));

    (void) pipeline->nvosd_display.release();
    gst_object_unref(probe_element);
}

TEST_F(overlay_probes_test, display_bitrate_overlay_probe_short_circuits_for_null_disabled_or_non_buffer_paths) {
    GstPadProbeInfo non_buffer_info{};
    non_buffer_info.type = GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM;

    EXPECT_EQ(display_bitrate_overlay_probe(nullptr, &non_buffer_info, nullptr), GST_PAD_PROBE_OK);

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    pipeline->draw_bitrate = false;
    pipeline->draw_bitrate_large = false;
    EXPECT_EQ(display_bitrate_overlay_probe(nullptr, &non_buffer_info, pipeline.get()), GST_PAD_PROBE_OK);

    pipeline->draw_bitrate = true;
    EXPECT_EQ(display_bitrate_overlay_probe(nullptr, &non_buffer_info, pipeline.get()), GST_PAD_PROBE_OK);
}

TEST_F(overlay_probes_test, display_fps_overlay_probe_short_circuits_for_null_disabled_or_non_buffer_paths) {
    GstPadProbeInfo non_buffer_info{};
    non_buffer_info.type = GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM;

    EXPECT_EQ(display_fps_overlay_probe(nullptr, &non_buffer_info, nullptr), GST_PAD_PROBE_OK);

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    pipeline->draw_fps = false;
    EXPECT_EQ(display_fps_overlay_probe(nullptr, &non_buffer_info, pipeline.get()), GST_PAD_PROBE_OK);

    pipeline->draw_fps = true;
    EXPECT_EQ(display_fps_overlay_probe(nullptr, &non_buffer_info, pipeline.get()), GST_PAD_PROBE_OK);
}
