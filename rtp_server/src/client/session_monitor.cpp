// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "session_monitor.h"

#include <gst/gst.h>
#include <vector>

#include "../pipeline/pipeline.h"
#include "logging.h"

// Debug category for session monitoring and cleanup.
GST_DEBUG_CATEGORY_STATIC(session_monitor_category);
#define GST_CAT_DEFAULT session_monitor_category

void init_session_monitor_debug_category() {
    GST_DEBUG_CATEGORY_INIT(session_monitor_category, "session-monitor", 0, "Session Monitoring and Cleanup");
}

session_monitor::session_monitor() : monitor_thread_running(false), monitor_thread(std::nullopt) {}

session_monitor::~session_monitor() { stop_monitoring(); }

void session_monitor::start_monitoring(std::map<guint16, std::unique_ptr<client_session>> &client_sessions,
                                       std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines) {
    if (monitor_thread_running.load()) {
        sf::warning(session_monitor_category, "Session monitoring is already running");
        return;
    }

    monitor_thread_running = true;
    monitor_thread.emplace(&session_monitor::monitor_thread_function, this, std::ref(client_sessions),
                           std::ref(client_pipelines));

    sf::info(session_monitor_category, "Session monitoring thread started");
}

void session_monitor::stop_monitoring() {
    if (!monitor_thread_running.load()) {
        return;
    }

    sf::info(session_monitor_category, "Stopping session monitoring thread...");
    monitor_thread_running = false;

    if (monitor_thread.has_value()) {
        monitor_thread->join();
        monitor_thread.reset();
    }
    sf::info(session_monitor_category, "Session monitoring thread stopped");
}

void session_monitor::monitor_thread_function(
        std::map<guint16, std::unique_ptr<client_session>> &client_sessions,
        std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines) const {
    sf::info(session_monitor_category, "Session monitoring thread started");

    while (monitor_thread_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Check every 5 seconds

        if (!monitor_thread_running.load())
            break;

        // Check for inactive sessions, poor quality, and client disconnections
        std::vector<guint16> sessions_to_cleanup;

        for (auto &[port, session]: client_sessions) {
            // Check for session timeout (based on last activity)
            if (session->is_session_timeout()) {
                sf::warning(session_monitor_category, "Session timeout detected for port %u", port);
                session->should_terminate = true;
                sessions_to_cleanup.push_back(port);
                continue;
            }

            // Check for client disconnection based on RTP statistics
            if (session->rtp_monitor && session->rtp_monitor->should_terminate_session()) {
                sf::warning(session_monitor_category,
                            "Client disconnection detected for port %u based on RTP statistics", port);
                session->should_terminate = true;
                sessions_to_cleanup.push_back(port);
            }
        }

        // Clean up sessions marked for termination
        for (const auto port: sessions_to_cleanup) {
            cleanup_client(client_pipelines, client_sessions, port);
        }
    }

    sf::info(session_monitor_category, "Session monitoring thread stopped");
}

void session_monitor::cleanup_client(std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines,
                                     std::map<guint16, std::unique_ptr<client_session>> &client_sessions,
                                     const guint16 port) {
    const auto pipeline_it = client_pipelines.find(port);
    const auto session_it = client_sessions.find(port);

    if (pipeline_it != client_pipelines.end()) {
        sf::info(session_monitor_category, "Cleaning up client on port %u", port);

        // Log final session statistics
        if (session_it != client_sessions.end()) {
            session_it->second->log_session_info();
        }

        // Write test mode summaries before stopping the pipeline, since
        // stop_pipeline() will terminate processing threads and the pipeline
        // destructor will destroy the test_mode_ctx.
        rtp_pipeline *pipeline = pipeline_it->second.get();
        if (pipeline->test_mode_ctx) {
            sf::info(session_monitor_category, "Writing test mode summary for port %u before session cleanup", port);
            const auto summary = build_test_mode_run_summary(*pipeline->test_mode_ctx);
            const auto frame_results = build_test_mode_frame_results(*pipeline->test_mode_ctx);
            const auto semseg_summary = build_semseg_test_mode_run_summary(*pipeline->test_mode_ctx);
            const auto semseg_frame_results = build_semseg_test_mode_frame_results(*pipeline->test_mode_ctx);
            log_test_mode_run_summary(summary, port, session_monitor_category);
            log_semseg_test_mode_run_summary(semseg_summary, port, session_monitor_category);
            if (pipeline->server_config) {
                if (pipeline->server_config->test_mode_output_md.has_value()) {
                    if (!write_test_mode_run_summary_markdown(summary, frame_results, port,
                                                              pipeline->server_config->test_mode_output_md.value(),
                                                              session_monitor_category)) {
                        sf::warning(session_monitor_category, "Test mode markdown summary write failed for port %u",
                                    port);
                    }
                }
                if (pipeline->server_config->test_mode_output_json.has_value()) {
                    if (!write_test_mode_run_summary_json(summary, frame_results, port,
                                                          pipeline->server_config->test_mode_output_json.value(),
                                                          session_monitor_category)) {
                        sf::warning(session_monitor_category, "Test mode JSON summary write failed for port %u", port);
                    }
                }
                if (pipeline->server_config->semseg_output_md.has_value()) {
                    if (!write_semseg_test_mode_run_summary_markdown(semseg_summary, semseg_frame_results, port,
                                                                     pipeline->server_config->semseg_output_md.value(),
                                                                     session_monitor_category)) {
                        sf::warning(session_monitor_category,
                                    "SemSeg test mode markdown summary write failed for port %u", port);
                    }
                }
                if (pipeline->server_config->semseg_output_json.has_value()) {
                    if (!write_semseg_test_mode_run_summary_json(semseg_summary, semseg_frame_results, port,
                                                                 pipeline->server_config->semseg_output_json.value(),
                                                                 session_monitor_category)) {
                        sf::warning(session_monitor_category, "SemSeg test mode JSON summary write failed for port %u",
                                    port);
                    }
                }
            }
        }

        // Stop the pipeline explicitly to ensure clean shutdown
        pipeline_it->second->stop_pipeline();

        // The rtp_pipeline destructor will handle cleaning up all transport resources.
        client_pipelines.erase(pipeline_it);
        sf::info(session_monitor_category, "Cleaned up pipeline and transport resources for client on port %u", port);
    }

    if (session_it != client_sessions.end()) {
        client_sessions.erase(session_it);
        sf::info(session_monitor_category, "Cleaned up session resources for client on port %u", port);
    }
}
