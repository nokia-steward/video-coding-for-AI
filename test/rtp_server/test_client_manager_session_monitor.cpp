// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <thread>

#include "../../rtp_server/src/client/client_manager.h"
#include "../../rtp_server/src/client/client_session.h"
#include "../../rtp_server/src/client/session_monitor.h"
#include "../../rtp_server/src/pipeline/pipeline.h"
#include "../../rtp_server/src/transport/transport_interface.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    class client_lifecycle_test : public GStreamerTestFixtureRtpServer {
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
    };
} // namespace

TEST_F(client_lifecycle_test, client_manager_does_not_store_pipeline_when_creation_fails) {
    transport_context ctx{};
    ctx.functions.get_transport_type = []() { return transport_type::rtp; };

    client_manager manager(loop, rtp_monitoring_config{}, rtp_output_config{}, std::nullopt,
                           sf::background_color::green, ctx, false, false, false, nullptr);
    manager.create_client_pipeline(5004);

    EXPECT_TRUE(manager.get_client_pipelines().empty());
    EXPECT_TRUE(manager.get_client_sessions().empty());
    EXPECT_FALSE(manager.get_session_monitor().is_monitoring());
}

TEST_F(client_lifecycle_test, session_monitor_start_and_stop_toggles_monitoring_state) {
    session_monitor monitor;
    std::map<guint16, std::unique_ptr<client_session>> sessions;
    std::map<guint16, std::unique_ptr<rtp_pipeline>> pipelines;

    monitor.start_monitoring(sessions, pipelines);
    EXPECT_TRUE(monitor.is_monitoring());

    const auto stop_start = std::chrono::steady_clock::now();
    monitor.stop_monitoring();
    const auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - stop_start);

    EXPECT_FALSE(monitor.is_monitoring());
    EXPECT_LT(elapsed.count(), 7);
}

TEST_F(client_lifecycle_test, session_monitor_stop_is_noop_when_not_running) {
    session_monitor monitor;
    EXPECT_FALSE(monitor.is_monitoring());
    EXPECT_NO_THROW(monitor.stop_monitoring());
    EXPECT_FALSE(monitor.is_monitoring());
}

TEST_F(client_lifecycle_test, client_session_constructor_resolves_pipeline_pointer_when_present) {
    init_client_session_debug_category();

    transport_context ctx{};
    ctx.functions.get_transport_type = []() { return transport_type::rtp; };

    std::map<guint16, std::unique_ptr<rtp_pipeline>> pipelines;
    pipelines.emplace(5004,
                      std::make_unique<rtp_pipeline>(5004, loop, rtp_output_config{}, std::nullopt,
                                                     sf::background_color::green, ctx, false, false, false, nullptr));
    ASSERT_NE(pipelines.at(5004), nullptr);

    const auto *expected_pipeline_ptr = pipelines.at(5004).get();
    const rtp_monitoring_config monitoring_config{};
    client_session session(5004, pipelines, monitoring_config);
    EXPECT_EQ(session.pipeline, expected_pipeline_ptr);
}

TEST_F(client_lifecycle_test, client_session_timeout_and_activity_updates_are_consistent) {
    std::map<guint16, std::unique_ptr<rtp_pipeline>> pipelines;
    const rtp_monitoring_config monitoring_config{};
    client_session session(5006, pipelines, monitoring_config);

    EXPECT_FALSE(session.is_session_timeout(std::chrono::seconds(1)));

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_TRUE(session.is_session_timeout(std::chrono::seconds(0)));

    session.update_activity();
    EXPECT_FALSE(session.is_session_timeout(std::chrono::seconds(1)));

    session.update_rtp_statistics(1200U, 42U, 7U);
    EXPECT_FALSE(session.is_session_timeout(std::chrono::seconds(1)));
}
