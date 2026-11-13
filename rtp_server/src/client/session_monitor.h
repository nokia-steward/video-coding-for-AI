// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <thread>

#include "client_session.h"

// Forward declarations
struct rtp_pipeline;

/**
 * @brief Session monitoring and cleanup management.
 *
 * This class handles monitoring client sessions for timeouts,
 * poor RTP quality, and periodic cleanup operations.
 */
struct session_monitor {
    /**
     * @brief Constructor.
     */
    session_monitor();

    /**
     * @brief Destructor.
     */
    ~session_monitor();

    // Disable copy constructor and assignment operator
    session_monitor(const session_monitor &) = delete;
    session_monitor &operator=(const session_monitor &) = delete;

    /**
     * @brief Start monitoring client sessions.
     * @param client_sessions Reference to the client sessions map.
     * @param client_pipelines Reference to the client pipelines map.
     */
    void start_monitoring(std::map<guint16, std::unique_ptr<client_session>> &client_sessions,
                          std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines);

    /**
     * @brief Stop monitoring and cleanup resources.
     */
    void stop_monitoring();

    /**
     * @brief Check if monitoring is currently active.
     * @return true if monitoring is active, false otherwise.
     */
    [[nodiscard]] bool is_monitoring() const { return monitor_thread_running.load(); }

private:
    /**
     * @brief The main monitoring thread function.
     * @param client_sessions Reference to the client sessions map.
     * @param client_pipelines Reference to the client pipelines map.
     */
    void monitor_thread_function(std::map<guint16, std::unique_ptr<client_session>> &client_sessions,
                                 std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines) const;

    /**
     * @brief Cleanup a specific client session.
     * @param client_pipelines Reference to the client pipelines map.
     * @param client_sessions Reference to the client sessions map.
     * @param port The port number of the client to cleanup.
     */
    static void cleanup_client(std::map<guint16, std::unique_ptr<rtp_pipeline>> &client_pipelines,
                               std::map<guint16, std::unique_ptr<client_session>> &client_sessions, guint16 port);

    std::atomic<bool> monitor_thread_running{false};
    std::optional<std::jthread> monitor_thread;
};

/**
 * @brief Initialize the session monitor debug category.
 */
void init_session_monitor_debug_category();
