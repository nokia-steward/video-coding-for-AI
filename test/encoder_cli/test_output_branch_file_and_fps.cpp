// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/output_pipeline/elements.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/display/fps_overlay.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/file_h265/file_h265_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/file_mp4/file_mp4_branch.h"
#include "../../encoder_cli/src/gst/output_pipeline/output_branches/output_branch_facade.h"
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

    void release_if_set(GstElement *&element) {
        if (element != nullptr) {
            gst_object_unref(element);
            element = nullptr;
        }
    }

    void cleanup_h265_elements(output_pipeline_elements &elements) {
        release_if_set(elements.queue_file);
        release_if_set(elements.sink_file);
    }

    void cleanup_mp4_elements(output_pipeline_elements &elements) {
        release_if_set(elements.queue_mp4);
        release_if_set(elements.parser_mp4);
        release_if_set(elements.muxer_mp4);
        release_if_set(elements.sink_mp4);
    }
} // namespace

class OutputBranchFileAndFpsTest : public GStreamerTestFixture {};

TEST_F(OutputBranchFileAndFpsTest, setup_fps_overlay_handles_disabled_and_invalid_converter) {
    auto disabled_config = make_base_config();
    disabled_config.debug.draw_fps = false;
    global_data disabled_data(std::move(disabled_config));
    output_branch_facade facade_disabled_data(disabled_data);
    EXPECT_TRUE(setup_fps_overlay(nullptr, disabled_data));

    auto enabled_config = make_base_config();
    enabled_config.debug.draw_fps = true;
    global_data enabled_data(std::move(enabled_config));
    output_branch_facade facade_enabled_data(enabled_data);
    EXPECT_FALSE(setup_fps_overlay(nullptr, enabled_data));

    sf::unique_gst_element_ptr converter_without_src(gst_element_factory_make("fakesink", "fps-no-src-pad"));
    ASSERT_NE(converter_without_src, nullptr);
    EXPECT_FALSE(setup_fps_overlay(converter_without_src.get(), enabled_data));
}

TEST_F(OutputBranchFileAndFpsTest, setup_fps_overlay_succeeds_with_identity_converter_and_is_clearable) {
    auto config = make_base_config();
    config.debug.draw_fps = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    sf::unique_gst_element_ptr converter(gst_element_factory_make("identity", "fps-identity-converter"));
    ASSERT_NE(converter, nullptr);
    EXPECT_TRUE(setup_fps_overlay(converter.get(), data));
    EXPECT_NO_THROW(clear_fps_tracker());
}

TEST_F(OutputBranchFileAndFpsTest, init_fps_overlay_debug_category_and_setup_fps_overlay_disabled_path_are_safe) {
    init_fps_overlay_debug_category();

    auto config = make_base_config();
    config.debug.draw_fps = false;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    EXPECT_TRUE(setup_fps_overlay(nullptr, data));
    EXPECT_NO_THROW(clear_fps_tracker());
}

TEST_F(OutputBranchFileAndFpsTest, format_fps_string_behavior_is_validated_through_setup_fps_overlay_paths) {
    auto config = make_base_config();
    config.debug.draw_fps = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    sf::unique_gst_element_ptr converter(gst_element_factory_make("identity", "fps-format-string-coverage"));
    ASSERT_NE(converter, nullptr);
    EXPECT_TRUE(setup_fps_overlay(converter.get(), data));
    EXPECT_NO_THROW(clear_fps_tracker());
}

TEST_F(OutputBranchFileAndFpsTest,
       configure_file_h265_branch_elements_uses_default_framerate_when_input_is_non_positive) {
    const auto context = create_file_h265_branch_context();
    const auto &functions = context.functions;

    auto config = make_base_config();
    config.debug.enable_h265_branch = true;
    config.framerate = -1;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.queue_file = gst_element_factory_make("queue", "file-h265-default-framerate-queue");
    ASSERT_NE(elements.queue_file, nullptr);

    ASSERT_TRUE(functions.configure_elements(facade_data, elements));

    guint max_size_buffers = 0;
    gint leaky = -1;
    g_object_get(G_OBJECT(elements.queue_file), "max-size-buffers", &max_size_buffers, "leaky", &leaky, nullptr);
    EXPECT_EQ(max_size_buffers, 120U);
    EXPECT_EQ(leaky, static_cast<int>(queue_leaky_mode::no_leak));

    cleanup_h265_elements(elements);
}

TEST_F(OutputBranchFileAndFpsTest, create_file_h265_branch_elements_and_link_file_h265_branch_guard_paths) {
    const auto context = create_file_h265_branch_context();
    const auto &functions = context.functions;

    auto config = make_base_config();
    config.debug.enable_h265_branch = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    ASSERT_TRUE(functions.create_elements(facade_data, elements));
    ASSERT_NE(elements.queue_file, nullptr);
    ASSERT_NE(elements.sink_file, nullptr);
    EXPECT_TRUE(functions.link_branch(elements));

    cleanup_h265_elements(elements);
}

TEST_F(OutputBranchFileAndFpsTest,
       configure_file_mp4_branch_elements_uses_default_framerate_when_input_is_non_positive) {
    const auto context = create_file_mp4_branch_context();
    const auto &functions = context.functions;

    auto config = make_base_config();
    config.debug.enable_mp4_branch = true;
    config.framerate = 0;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    elements.queue_mp4 = gst_element_factory_make("queue", "file-mp4-default-framerate-queue");
    ASSERT_NE(elements.queue_mp4, nullptr);

    ASSERT_TRUE(functions.configure_elements(facade_data, elements));

    guint max_size_buffers = 0;
    gint leaky = -1;
    g_object_get(G_OBJECT(elements.queue_mp4), "max-size-buffers", &max_size_buffers, "leaky", &leaky, nullptr);
    EXPECT_EQ(max_size_buffers, 120U);
    EXPECT_EQ(leaky, static_cast<int>(queue_leaky_mode::no_leak));

    cleanup_mp4_elements(elements);
}

TEST_F(OutputBranchFileAndFpsTest, create_file_mp4_branch_elements_and_link_file_mp4_branch_guard_paths) {
    const auto context = create_file_mp4_branch_context();
    const auto &functions = context.functions;

    auto config = make_base_config();
    config.debug.enable_mp4_branch = true;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);

    output_pipeline_elements elements{};
    const bool created = functions.create_elements(facade_data, elements);
    if (!created) {
        GTEST_SKIP() << "Required mp4 branch plugins unavailable in this environment";
    }

    ASSERT_NE(elements.queue_mp4, nullptr);
    ASSERT_NE(elements.parser_mp4, nullptr);
    ASSERT_NE(elements.muxer_mp4, nullptr);
    ASSERT_NE(elements.sink_mp4, nullptr);
    EXPECT_TRUE(functions.link_branch(elements));

    cleanup_mp4_elements(elements);
}

TEST_F(OutputBranchFileAndFpsTest, add_file_h265_branch_to_pipeline_is_noop_when_is_file_h265_branch_enabled_is_false) {
    const auto context = create_file_h265_branch_context();
    const auto &functions = context.functions;

    auto config = make_base_config();
    config.debug.enable_h265_branch = false;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("file-h265-disabled-pipeline"));
    ASSERT_NE(data.output_pipeline.get(), nullptr);

    output_pipeline_elements elements{};
    elements.queue_file = gst_element_factory_make("queue", "file-h265-disabled-queue");
    elements.sink_file = gst_element_factory_make("fakesink", "file-h265-disabled-sink");
    ASSERT_NE(elements.queue_file, nullptr);
    ASSERT_NE(elements.sink_file, nullptr);

    functions.add_to_pipeline(facade_data, elements);
    EXPECT_EQ(gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "file-h265-disabled-queue"), nullptr);
    EXPECT_EQ(gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "file-h265-disabled-sink"), nullptr);

    cleanup_h265_elements(elements);
}

TEST_F(OutputBranchFileAndFpsTest, add_file_mp4_branch_to_pipeline_is_noop_when_is_file_mp4_branch_enabled_is_false) {
    const auto context = create_file_mp4_branch_context();
    const auto &functions = context.functions;

    auto config = make_base_config();
    config.debug.enable_mp4_branch = false;
    global_data data(std::move(config));
    output_branch_facade facade_data(data);
    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("file-mp4-disabled-pipeline"));
    ASSERT_NE(data.output_pipeline.get(), nullptr);

    output_pipeline_elements elements{};
    elements.queue_mp4 = gst_element_factory_make("queue", "file-mp4-disabled-queue");
    elements.parser_mp4 = gst_element_factory_make("identity", "file-mp4-disabled-parser");
    elements.muxer_mp4 = gst_element_factory_make("identity", "file-mp4-disabled-muxer");
    elements.sink_mp4 = gst_element_factory_make("fakesink", "file-mp4-disabled-sink");
    ASSERT_NE(elements.queue_mp4, nullptr);
    ASSERT_NE(elements.parser_mp4, nullptr);
    ASSERT_NE(elements.muxer_mp4, nullptr);
    ASSERT_NE(elements.sink_mp4, nullptr);

    functions.add_to_pipeline(facade_data, elements);
    EXPECT_EQ(gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "file-mp4-disabled-queue"), nullptr);
    EXPECT_EQ(gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "file-mp4-disabled-sink"), nullptr);

    cleanup_mp4_elements(elements);
}

TEST_F(OutputBranchFileAndFpsTest, file_h265_branch_guard_and_configuration_paths) {
    const auto context = create_file_h265_branch_context();
    const auto &functions = context.functions;

    auto disabled_config = make_base_config();
    disabled_config.debug.enable_h265_branch = false;
    global_data disabled_data(std::move(disabled_config));
    output_branch_facade facade_disabled_data(disabled_data);
    output_pipeline_elements disabled_elements{};

    EXPECT_FALSE(functions.is_enabled(facade_disabled_data));
    EXPECT_TRUE(functions.create_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.configure_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.link_branch(disabled_elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, disabled_elements));
    EXPECT_TRUE(functions.setup_signals(facade_disabled_data, disabled_elements));
    EXPECT_STREQ(functions.get_branch_name(), "file-h265");

    auto enabled_config = make_base_config();
    enabled_config.debug.enable_h265_branch = true;
    enabled_config.framerate = 25;
    global_data enabled_data(std::move(enabled_config));
    output_branch_facade facade_enabled_data(enabled_data);
    output_pipeline_elements enabled_elements{};
    ASSERT_TRUE(functions.is_enabled(facade_enabled_data));
    ASSERT_TRUE(functions.create_elements(facade_enabled_data, enabled_elements));
    ASSERT_NE(enabled_elements.queue_file, nullptr);
    ASSERT_NE(enabled_elements.sink_file, nullptr);
    ASSERT_TRUE(functions.configure_elements(facade_enabled_data, enabled_elements));

    guint max_size_buffers = 0;
    gint leaky = -1;
    g_object_get(G_OBJECT(enabled_elements.queue_file), "max-size-buffers", &max_size_buffers, "leaky", &leaky,
                 nullptr);
    EXPECT_EQ(max_size_buffers, 50U);
    EXPECT_EQ(leaky, static_cast<int>(queue_leaky_mode::no_leak));

    EXPECT_FALSE(functions.link_to_tee(nullptr, enabled_elements));

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "file-h265-tee"));
    ASSERT_NE(tee, nullptr);
    EXPECT_TRUE(functions.link_to_tee(tee.get(), enabled_elements));

    enabled_data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("file-h265-output-pipeline"));
    ASSERT_NE(enabled_data.output_pipeline.get(), nullptr);
    functions.add_to_pipeline(facade_enabled_data, enabled_elements);
    // Ownership moved to output_pipeline bin.
    enabled_elements.queue_file = nullptr;
    enabled_elements.sink_file = nullptr;

    GstElement *queue_in_bin = gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "q-file");
    GstElement *sink_in_bin = gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "s-file");
    ASSERT_NE(queue_in_bin, nullptr);
    ASSERT_NE(sink_in_bin, nullptr);
    gst_object_unref(queue_in_bin);
    gst_object_unref(sink_in_bin);

    output_pipeline_elements incomplete{};
    incomplete.queue_file = gst_element_factory_make("queue", "file-h265-incomplete-queue");
    ASSERT_NE(incomplete.queue_file, nullptr);
    EXPECT_FALSE(functions.link_branch(incomplete));
    cleanup_h265_elements(incomplete);

    cleanup_h265_elements(enabled_elements);
}

TEST_F(OutputBranchFileAndFpsTest, file_mp4_branch_guard_and_configuration_paths) {
    const auto context = create_file_mp4_branch_context();
    const auto &functions = context.functions;

    auto disabled_config = make_base_config();
    disabled_config.debug.enable_mp4_branch = false;
    global_data disabled_data(std::move(disabled_config));
    output_branch_facade facade_disabled_data(disabled_data);
    output_pipeline_elements disabled_elements{};

    EXPECT_FALSE(functions.is_enabled(facade_disabled_data));
    EXPECT_TRUE(functions.create_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.configure_elements(facade_disabled_data, disabled_elements));
    EXPECT_TRUE(functions.link_branch(disabled_elements));
    EXPECT_TRUE(functions.link_to_tee(nullptr, disabled_elements));
    EXPECT_TRUE(functions.setup_signals(facade_disabled_data, disabled_elements));
    EXPECT_STREQ(functions.get_branch_name(), "file-mp4");

    auto enabled_config = make_base_config();
    enabled_config.debug.enable_mp4_branch = true;
    enabled_config.framerate = 24;
    global_data enabled_data(std::move(enabled_config));
    output_branch_facade facade_enabled_data(enabled_data);
    output_pipeline_elements enabled_elements{};
    ASSERT_TRUE(functions.is_enabled(facade_enabled_data));
    ASSERT_TRUE(functions.create_elements(facade_enabled_data, enabled_elements));
    ASSERT_NE(enabled_elements.queue_mp4, nullptr);
    ASSERT_NE(enabled_elements.parser_mp4, nullptr);
    ASSERT_NE(enabled_elements.muxer_mp4, nullptr);
    ASSERT_NE(enabled_elements.sink_mp4, nullptr);
    ASSERT_TRUE(functions.configure_elements(facade_enabled_data, enabled_elements));

    guint max_size_buffers = 0;
    gint leaky = -1;
    g_object_get(G_OBJECT(enabled_elements.queue_mp4), "max-size-buffers", &max_size_buffers, "leaky", &leaky, nullptr);
    EXPECT_EQ(max_size_buffers, 48U);
    EXPECT_EQ(leaky, static_cast<int>(queue_leaky_mode::no_leak));

    EXPECT_FALSE(functions.link_to_tee(nullptr, enabled_elements));

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "file-mp4-tee"));
    ASSERT_NE(tee, nullptr);
    EXPECT_TRUE(functions.link_to_tee(tee.get(), enabled_elements));

    enabled_data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("file-mp4-output-pipeline"));
    ASSERT_NE(enabled_data.output_pipeline.get(), nullptr);
    functions.add_to_pipeline(facade_enabled_data, enabled_elements);
    // Ownership moved to output_pipeline bin.
    enabled_elements.queue_mp4 = nullptr;
    enabled_elements.parser_mp4 = nullptr;
    enabled_elements.muxer_mp4 = nullptr;
    enabled_elements.sink_mp4 = nullptr;

    GstElement *queue_in_bin = gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "q-mp4");
    GstElement *parser_in_bin = gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "p-mp4");
    GstElement *muxer_in_bin = gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "m-mp4");
    GstElement *sink_in_bin = gst_bin_get_by_name(GST_BIN(enabled_data.output_pipeline.get()), "s-mp4");
    ASSERT_NE(queue_in_bin, nullptr);
    ASSERT_NE(parser_in_bin, nullptr);
    ASSERT_NE(muxer_in_bin, nullptr);
    ASSERT_NE(sink_in_bin, nullptr);
    gst_object_unref(queue_in_bin);
    gst_object_unref(parser_in_bin);
    gst_object_unref(muxer_in_bin);
    gst_object_unref(sink_in_bin);

    output_pipeline_elements incomplete{};
    incomplete.queue_mp4 = gst_element_factory_make("queue", "file-mp4-incomplete-queue");
    ASSERT_NE(incomplete.queue_mp4, nullptr);
    EXPECT_FALSE(functions.link_branch(incomplete));
    cleanup_mp4_elements(incomplete);

    cleanup_mp4_elements(enabled_elements);
}
