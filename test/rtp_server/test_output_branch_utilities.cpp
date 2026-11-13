// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../../libvcai/include/gst_raii.h"
#include "../../rtp_server/src/pipeline/output/branch_factory.h"
#include "../../rtp_server/src/pipeline/output/branch_utils.h"
#include "../../rtp_server/src/pipeline/output/debug_frames/debug_frames_branch.h"
#include "../../rtp_server/src/pipeline/output/file/file_branch.h"
#include "../../rtp_server/src/pipeline/output/rtp_output/rtp_output_branch.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../../rtp_server/src/transport/transport_interface.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    optional_branch_status always_enabled([[maybe_unused]] const rtp_pipeline &pipeline) {
        return optional_branch_status::enabled;
    }

    optional_branch_status always_disabled([[maybe_unused]] const rtp_pipeline &pipeline) {
        return optional_branch_status::disabled;
    }

    class output_branch_utilities_test : public GStreamerTestFixtureRtpServer {
    protected:
        GMainLoop *loop = nullptr;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            loop = g_main_loop_new(nullptr, FALSE);
        }

        void TearDown() override {
            if (loop != nullptr) {
                g_main_loop_unref(loop);
                loop = nullptr;
            }
            GStreamerTestFixtureRtpServer::TearDown();
        }

        [[nodiscard]] std::unique_ptr<rtp_pipeline>
        make_pipeline(const rtp_server_config *server_config = nullptr) const {
            transport_context ctx{};
            ctx.functions.get_transport_type = []() { return transport_type::rtp; };
            return std::make_unique<rtp_pipeline>(5004, loop, rtp_output_config{}, std::nullopt,
                                                  sf::background_color::green, std::move(ctx), false, false, false,
                                                  server_config);
        }
    };
} // namespace

TEST_F(output_branch_utilities_test, branch_utils_validate_required_elements_and_gate_decisions) {
    EXPECT_FALSE(has_all_required_elements({nullptr}));
    sf::unique_gst_element_ptr element(gst_element_factory_make("identity", "branch-utils-id"));
    ASSERT_NE(element, nullptr);
    EXPECT_TRUE(has_all_required_elements({element.get()}));
    EXPECT_FALSE(has_all_required_elements({element.get(), nullptr}));

    const auto invalid_decision = get_optional_branch_gate_decision(nullptr, always_enabled);
    EXPECT_FALSE(invalid_decision.is_valid_pipeline);
    EXPECT_FALSE(invalid_decision.should_execute);

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    const auto enabled_decision = get_optional_branch_gate_decision(pipeline.get(), always_enabled);
    EXPECT_TRUE(enabled_decision.is_valid_pipeline);
    EXPECT_TRUE(enabled_decision.should_execute);

    const auto disabled_decision = get_optional_branch_gate_decision(pipeline.get(), always_disabled);
    EXPECT_TRUE(disabled_decision.is_valid_pipeline);
    EXPECT_FALSE(disabled_decision.should_execute);

    const auto null_resolver_decision = get_optional_branch_gate_decision(pipeline.get(), nullptr);
    EXPECT_FALSE(null_resolver_decision.is_valid_pipeline);
    EXPECT_FALSE(null_resolver_decision.should_execute);
}

TEST_F(output_branch_utilities_test, branch_utils_configure_queue_and_link_tee_to_queue_paths) {
    configure_queue_element(nullptr, 1000, 5, 100);

    sf::unique_gst_element_ptr queue(gst_element_factory_make("queue", "branch-utils-queue"));
    ASSERT_NE(queue, nullptr);
    configure_queue_element(queue.get(), 1024, 11, 333);

    guint max_size_bytes = 0;
    guint max_size_buffers = 0;
    guint64 max_size_time = 0;
    gint leaky = 0;
    g_object_get(G_OBJECT(queue.get()), "max-size-bytes", &max_size_bytes, "max-size-buffers", &max_size_buffers,
                 "max-size-time", &max_size_time, "leaky", &leaky, nullptr);
    EXPECT_EQ(max_size_bytes, 1024U);
    EXPECT_EQ(max_size_buffers, 11U);
    EXPECT_EQ(max_size_time, 333U);
    EXPECT_EQ(leaky, 2);

    EXPECT_FALSE(link_tee_to_branch_queue(nullptr, "test", 5004, nullptr, queue.get()));
    EXPECT_FALSE(link_tee_to_branch_queue(nullptr, "test", 5004, queue.get(), nullptr));

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "branch-utils-tee"));
    ASSERT_NE(tee, nullptr);
    EXPECT_TRUE(link_tee_to_branch_queue(nullptr, "test", 5004, tee.get(), queue.get()));

    sf::unique_gst_element_ptr non_tee(gst_element_factory_make("identity", "branch-utils-not-tee"));
    ASSERT_NE(non_tee, nullptr);
    EXPECT_FALSE(link_tee_to_branch_queue(nullptr, "test", 5004, non_tee.get(), queue.get()));
}

TEST_F(output_branch_utilities_test, file_branch_handles_disabled_and_null_guard_paths) {
    const auto context = create_file_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.is_enabled(nullptr));
    EXPECT_FALSE(functions.create_elements(nullptr));
    EXPECT_FALSE(functions.configure_elements(nullptr));
    EXPECT_FALSE(functions.link_branch(nullptr));
    EXPECT_FALSE(functions.link_to_tee(nullptr, nullptr));
    EXPECT_FALSE(functions.add_to_pipeline(nullptr, nullptr));
    EXPECT_NO_THROW(functions.cleanup(nullptr));
    EXPECT_STREQ(functions.get_branch_name(), "file");

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_FALSE(functions.is_enabled(pipeline.get()));
    EXPECT_TRUE(functions.create_elements(pipeline.get()));
    EXPECT_TRUE(functions.configure_elements(pipeline.get()));
    EXPECT_TRUE(functions.link_branch(pipeline.get()));

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "file-branch-disabled-tee"));
    sf::unique_gst_element_ptr output_pipeline(gst_pipeline_new("file-branch-disabled-output"));
    ASSERT_NE(tee, nullptr);
    ASSERT_NE(output_pipeline, nullptr);
    EXPECT_TRUE(functions.link_to_tee(pipeline.get(), tee.get()));
    EXPECT_TRUE(functions.add_to_pipeline(pipeline.get(), output_pipeline.get()));
}

TEST_F(output_branch_utilities_test, file_branch_enabled_path_configures_sink_and_links_branch) {
    const auto context = create_file_branch_context();
    const auto &functions = context.functions;

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = "/tmp";

    pipeline->queue_file = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
    pipeline->h265_encoder = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", nullptr));
    pipeline->filesink = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("filesink", nullptr));
    ASSERT_NE(pipeline->queue_file.get(), nullptr);
    ASSERT_NE(pipeline->h265_encoder.get(), nullptr);
    ASSERT_NE(pipeline->filesink.get(), nullptr);

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "file-branch-enabled-tee"));
    sf::unique_gst_element_ptr output_pipeline(gst_pipeline_new("file-branch-enabled-output"));
    ASSERT_NE(tee, nullptr);
    ASSERT_NE(output_pipeline, nullptr);

    EXPECT_TRUE(functions.is_enabled(pipeline.get()));
    EXPECT_TRUE(functions.configure_elements(pipeline.get()));
    EXPECT_TRUE(functions.link_branch(pipeline.get()));
    EXPECT_TRUE(functions.link_to_tee(pipeline.get(), tee.get()));
    EXPECT_TRUE(functions.add_to_pipeline(pipeline.get(), output_pipeline.get()));

    gchar *location = nullptr;
    g_object_get(G_OBJECT(pipeline->filesink.get()), "location", &location, nullptr);
    ASSERT_NE(location, nullptr);
    const std::string file_location(location);
    g_free(location);
    EXPECT_NE(file_location.find("/tmp/"), std::string::npos);
    EXPECT_NE(file_location.find(".h265"), std::string::npos);
}

TEST_F(output_branch_utilities_test, resolve_file_branch_status_gate_file_branch_create_file_branch_functions_paths) {
    const auto functions = create_file_branch_functions();
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    EXPECT_FALSE(functions.is_enabled(pipeline.get()));
    pipeline->output_directory = "/tmp";
    EXPECT_TRUE(functions.is_enabled(pipeline.get()));
}

TEST_F(output_branch_utilities_test, rtp_output_branch_handles_disabled_and_null_guard_paths) {
    const auto context = create_rtp_output_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.is_enabled(nullptr));
    EXPECT_FALSE(functions.create_elements(nullptr));
    EXPECT_FALSE(functions.configure_elements(nullptr));
    EXPECT_FALSE(functions.link_branch(nullptr));
    EXPECT_FALSE(functions.link_to_tee(nullptr, nullptr));
    EXPECT_FALSE(functions.add_to_pipeline(nullptr, nullptr));
    EXPECT_NO_THROW(functions.cleanup(nullptr));
    EXPECT_STREQ(functions.get_branch_name(), "rtp-output");

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_FALSE(functions.is_enabled(pipeline.get()));
    EXPECT_TRUE(functions.create_elements(pipeline.get()));
    EXPECT_TRUE(functions.configure_elements(pipeline.get()));
    EXPECT_TRUE(functions.link_branch(pipeline.get()));

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "rtp-output-disabled-tee"));
    sf::unique_gst_element_ptr output_pipeline(gst_pipeline_new("rtp-output-disabled-output"));
    ASSERT_NE(tee, nullptr);
    ASSERT_NE(output_pipeline, nullptr);
    EXPECT_TRUE(functions.link_to_tee(pipeline.get(), tee.get()));
    EXPECT_TRUE(functions.add_to_pipeline(pipeline.get(), output_pipeline.get()));

    pipeline->rtp_output.enabled = true;
    EXPECT_FALSE(functions.link_to_tee(pipeline.get(), nullptr));
}

TEST_F(output_branch_utilities_test, rtp_output_branch_enabled_path_configures_properties_and_links_branch) {
    const auto context = create_rtp_output_branch_context();
    const auto &functions = context.functions;

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    pipeline->rtp_output.enabled = true;
    pipeline->rtp_output.host = "127.0.0.1";
    pipeline->rtp_output.port = 5500;
    pipeline->rtp_output.payload_type = 112;

    EXPECT_TRUE(functions.is_enabled(pipeline.get()));
    EXPECT_FALSE(functions.configure_elements(pipeline.get()));
    EXPECT_FALSE(functions.link_branch(pipeline.get()));

    pipeline->queue_rtp_output = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", nullptr));
    pipeline->h265_encoder_rtp_output =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("identity", nullptr));
    pipeline->rtph265pay_rtp_output =
            sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("rtph265pay", nullptr));
    pipeline->rtp_output_sink = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("udpsink", nullptr));
    if (pipeline->rtph265pay_rtp_output.get() == nullptr || pipeline->rtp_output_sink.get() == nullptr) {
        GTEST_SKIP() << "Required GStreamer RTP plugins are unavailable in this environment";
    }
    ASSERT_NE(pipeline->queue_rtp_output.get(), nullptr);
    ASSERT_NE(pipeline->h265_encoder_rtp_output.get(), nullptr);

    sf::unique_gst_element_ptr tee(gst_element_factory_make("tee", "rtp-output-enabled-tee"));
    sf::unique_gst_element_ptr output_pipeline(gst_pipeline_new("rtp-output-enabled-output"));
    ASSERT_NE(tee, nullptr);
    ASSERT_NE(output_pipeline, nullptr);

    EXPECT_TRUE(functions.configure_elements(pipeline.get()));
    EXPECT_TRUE(functions.link_branch(pipeline.get()));
    EXPECT_TRUE(functions.link_to_tee(pipeline.get(), tee.get()));
    EXPECT_TRUE(functions.add_to_pipeline(pipeline.get(), output_pipeline.get()));

    guint queue_max_buffers = 0;
    g_object_get(G_OBJECT(pipeline->queue_rtp_output.get()), "max-size-buffers", &queue_max_buffers, nullptr);
    EXPECT_EQ(queue_max_buffers, 100U);

    guint payload_type = 0;
    gint config_interval = 0;
    g_object_get(G_OBJECT(pipeline->rtph265pay_rtp_output.get()), "pt", &payload_type, "config-interval",
                 &config_interval, nullptr);
    EXPECT_EQ(payload_type, 112U);
    EXPECT_EQ(config_interval, -1);

    gchar *host = nullptr;
    gint port = 0;
    gboolean async_value = TRUE;
    gboolean sync_value = TRUE;
    g_object_get(G_OBJECT(pipeline->rtp_output_sink.get()), "host", &host, "port", &port, "async", &async_value, "sync",
                 &sync_value, nullptr);
    ASSERT_NE(host, nullptr);
    EXPECT_STREQ(host, "127.0.0.1");
    EXPECT_EQ(port, 5500);
    EXPECT_EQ(async_value, FALSE);
    EXPECT_EQ(sync_value, FALSE);
    g_free(host);
}

TEST_F(output_branch_utilities_test,
       resolve_rtp_output_branch_status_gate_rtp_output_branch_create_rtp_output_branch_functions_paths) {
    const auto functions = create_rtp_output_branch_functions();
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_FALSE(functions.is_enabled(pipeline.get()));

    pipeline->rtp_output.enabled = true;
    pipeline->rtp_output.host = "127.0.0.1";
    pipeline->rtp_output.port = 6100;
    EXPECT_TRUE(functions.is_enabled(pipeline.get()));
}

TEST_F(output_branch_utilities_test, debug_frames_branch_enablement_and_guard_paths) {
    const auto context = create_debug_frames_branch_context();
    const auto &functions = context.functions;

    EXPECT_FALSE(functions.is_enabled(nullptr));
    EXPECT_TRUE(functions.create_elements(nullptr));
    EXPECT_TRUE(functions.configure_elements(nullptr));
    EXPECT_TRUE(functions.link_branch(nullptr));
    EXPECT_TRUE(functions.link_to_tee(nullptr, nullptr));
    EXPECT_TRUE(functions.add_to_pipeline(nullptr, nullptr));
    EXPECT_NO_THROW(functions.cleanup(nullptr));
    EXPECT_STREQ(functions.get_branch_name(), "debug-frames");

    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);
    EXPECT_FALSE(functions.is_enabled(pipeline.get()));
    EXPECT_TRUE(functions.create_elements(pipeline.get()));
    EXPECT_TRUE(functions.configure_elements(pipeline.get()));
    EXPECT_TRUE(functions.link_branch(pipeline.get()));
    EXPECT_TRUE(functions.link_to_tee(pipeline.get(), nullptr));
    EXPECT_TRUE(functions.add_to_pipeline(pipeline.get(), nullptr));

    rtp_server_config config{};
    config.save_frames = true;
    pipeline->server_config = &config;
    EXPECT_TRUE(functions.is_enabled(pipeline.get()));
    EXPECT_FALSE(functions.link_to_tee(pipeline.get(), nullptr));
}

TEST_F(output_branch_utilities_test, debug_frames_branch_configures_queue_sink_and_cleans_existing_directory) {
    const auto context = create_debug_frames_branch_context();
    const auto &functions = context.functions;

    rtp_server_config server_config{};
    server_config.save_frames = true;

    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto output_root = std::filesystem::temp_directory_path() / ("rtp_debug_frames_" + unique_suffix);
    std::filesystem::create_directories(output_root);

    auto pipeline = make_pipeline(&server_config);
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = output_root.string();

    const auto debug_dir = output_root / "debug_frames" / ("port_" + std::to_string(pipeline->get_port()));
    std::filesystem::create_directories(debug_dir);
    {
        std::ofstream stale_file(debug_dir / "stale.png");
        ASSERT_TRUE(stale_file.is_open());
        stale_file << "stale";
    }
    ASSERT_TRUE(std::filesystem::exists(debug_dir / "stale.png"));

    if (!functions.create_elements(pipeline.get())) {
        std::filesystem::remove_all(output_root);
        GTEST_SKIP() << "Required debug-frame plugins are unavailable in this environment";
    }

    EXPECT_TRUE(functions.configure_elements(pipeline.get()));
    EXPECT_FALSE(std::filesystem::exists(debug_dir / "stale.png"));

    guint queue_max_buffers = 0;
    guint queue_max_bytes = 1;
    guint64 queue_max_time = 1;
    g_object_get(G_OBJECT(pipeline->queue_debug_frames.get()), "max-size-buffers", &queue_max_buffers, "max-size-bytes",
                 &queue_max_bytes, "max-size-time", &queue_max_time, nullptr);
    EXPECT_EQ(queue_max_buffers, 120U);
    EXPECT_EQ(queue_max_bytes, 0U);
    EXPECT_EQ(queue_max_time, 0U);

    gboolean sink_async = TRUE;
    gint sink_max_files = -1;
    gchar *sink_location = nullptr;
    g_object_get(G_OBJECT(pipeline->debug_frames_sink.get()), "async", &sink_async, "max-files", &sink_max_files,
                 "location", &sink_location, nullptr);
    ASSERT_NE(sink_location, nullptr);
    EXPECT_EQ(sink_async, FALSE);
    EXPECT_EQ(sink_max_files, 0);
    EXPECT_EQ(std::string(sink_location), (debug_dir / "frame_%06d.png").string());
    g_free(sink_location);

    sf::unique_gst_element_ptr output_pipeline(gst_pipeline_new("debug-frames-output-pipeline"));
    ASSERT_NE(output_pipeline, nullptr);
    EXPECT_TRUE(functions.add_to_pipeline(pipeline.get(), output_pipeline.get()));

    std::filesystem::remove_all(output_root);
}

TEST_F(output_branch_utilities_test, debug_frames_branch_configure_fails_when_output_root_is_not_directory) {
    const auto context = create_debug_frames_branch_context();
    const auto &functions = context.functions;

    rtp_server_config server_config{};
    server_config.save_frames = true;

    const auto unique_suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temp_root = std::filesystem::temp_directory_path() / ("rtp_debug_frames_file_root_" + unique_suffix);
    std::filesystem::create_directories(temp_root);
    const auto output_file = temp_root / "not_a_directory";
    {
        std::ofstream file(output_file);
        ASSERT_TRUE(file.is_open());
        file << "x";
    }

    auto pipeline = make_pipeline(&server_config);
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = output_file.string();

    if (!functions.create_elements(pipeline.get())) {
        std::filesystem::remove_all(temp_root);
        GTEST_SKIP() << "Required debug-frame plugins are unavailable in this environment";
    }

    EXPECT_FALSE(functions.configure_elements(pipeline.get()));
    std::filesystem::remove_all(temp_root);
}

TEST_F(output_branch_utilities_test, debug_frames_branch_uses_larger_queue_in_ah0187_mode) {
    const auto context = create_debug_frames_branch_context();
    const auto &functions = context.functions;

    rtp_server_config server_config{};
    server_config.save_frames = true;

    auto pipeline = make_pipeline(&server_config);
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = std::filesystem::temp_directory_path().string();

    if (!functions.create_elements(pipeline.get())) {
        GTEST_SKIP() << "Required debug-frame plugins are unavailable in this environment";
    }

    setenv("SF_TET_COMPARISON_SEMSEG_MODE", "1", 1);
    ASSERT_TRUE(functions.configure_elements(pipeline.get()));
    unsetenv("SF_TET_COMPARISON_SEMSEG_MODE");

    guint queue_max_buffers = 0;
    g_object_get(G_OBJECT(pipeline->queue_debug_frames.get()), "max-size-buffers", &queue_max_buffers, nullptr);
    EXPECT_EQ(queue_max_buffers, 512U);
}

TEST_F(output_branch_utilities_test, debug_frames_branch_uses_post_osd_tee_when_available) {
    const auto context = create_debug_frames_branch_context();
    const auto &functions = context.functions;

    rtp_server_config server_config{};
    server_config.save_frames = true;

    auto pipeline = make_pipeline(&server_config);
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = std::filesystem::temp_directory_path().string();
    pipeline->post_osd_tee = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("tee", "post-osd-tee"));
    ASSERT_NE(pipeline->post_osd_tee.get(), nullptr);

    if (!functions.create_elements(pipeline.get())) {
        GTEST_SKIP() << "Required debug-frame plugins are unavailable in this environment";
    }
    ASSERT_TRUE(functions.configure_elements(pipeline.get()));

    // tee_element arg is null here; branch should use post_osd_tee instead.
    EXPECT_TRUE(functions.link_to_tee(pipeline.get(), nullptr));
}

TEST_F(output_branch_utilities_test, debug_frames_branch_uses_pre_osd_tee_in_ah0187_mode) {
    const auto context = create_debug_frames_branch_context();
    const auto &functions = context.functions;

    rtp_server_config server_config{};
    server_config.save_frames = true;

    auto pipeline = make_pipeline(&server_config);
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = std::filesystem::temp_directory_path().string();
    pipeline->post_osd_tee = sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("tee", "post-osd-tee"));
    ASSERT_NE(pipeline->post_osd_tee.get(), nullptr);

    sf::unique_gst_element_ptr pre_osd_tee(gst_element_factory_make("tee", "pre-osd-tee"));
    ASSERT_NE(pre_osd_tee, nullptr);

    if (!functions.create_elements(pipeline.get())) {
        GTEST_SKIP() << "Required debug-frame plugins are unavailable in this environment";
    }
    ASSERT_TRUE(functions.configure_elements(pipeline.get()));

    setenv("SF_TET_COMPARISON_SEMSEG_MODE", "1", 1);
    EXPECT_TRUE(functions.link_to_tee(pipeline.get(), pre_osd_tee.get()));
    unsetenv("SF_TET_COMPARISON_SEMSEG_MODE");
}

TEST_F(output_branch_utilities_test, is_branch_enabled_prepare_debug_directory_and_link_debug_branch) {
    const auto context = create_debug_frames_branch_context();
    const auto &functions = context.functions;

    rtp_server_config server_config{};
    server_config.save_frames = true;
    auto pipeline = make_pipeline(&server_config);
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = std::filesystem::temp_directory_path().string();

    if (!functions.create_elements(pipeline.get())) {
        GTEST_SKIP() << "Required debug-frame plugins are unavailable in this environment";
    }
    EXPECT_TRUE(functions.configure_elements(pipeline.get()));
    EXPECT_TRUE(functions.link_branch(pipeline.get()));
}

TEST_F(output_branch_utilities_test, branch_factory_returns_empty_for_null_pipeline) {
    const auto branches = create_output_branch_contexts(nullptr);
    EXPECT_TRUE(branches.empty());
}

TEST_F(output_branch_utilities_test, branch_factory_includes_only_display_when_optional_outputs_disabled) {
    auto pipeline = make_pipeline();
    ASSERT_NE(pipeline, nullptr);

    const auto branches = create_output_branch_contexts(pipeline.get());
    ASSERT_EQ(branches.size(), 1U);
    ASSERT_TRUE(branches[0].functions.get_branch_name);
    EXPECT_STREQ(branches[0].functions.get_branch_name(), "display");
}

TEST_F(output_branch_utilities_test, branch_factory_enables_all_branches_when_pipeline_flags_are_set) {
    rtp_server_config server_config{};
    server_config.save_frames = true;

    auto pipeline = make_pipeline(&server_config);
    ASSERT_NE(pipeline, nullptr);
    pipeline->output_directory = "/tmp";
    pipeline->rtp_output.enabled = true;

    const auto branches = create_output_branch_contexts(pipeline.get());
    ASSERT_EQ(branches.size(), 4U);

    std::vector<std::string> names;
    names.reserve(branches.size());
    for (const auto &branch: branches) {
        ASSERT_TRUE(branch.functions.get_branch_name);
        names.emplace_back(branch.functions.get_branch_name());
    }

    EXPECT_NE(std::find(names.begin(), names.end(), "display"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "file"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "rtp-output"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "debug-frames"), names.end());
}
