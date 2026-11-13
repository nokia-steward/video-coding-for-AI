// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../bridge/context/memory_policy_runtime.h"

/**
 * @file memory_monitor.height
 * @brief Host and GPU memory monitoring utilities for runtime diagnostics.
 */

/**
 * @struct memory_stats
 * @brief Host and GPU memory statistics.
 */
struct memory_stats {
    // Host memory (process)
    uint64_t process_rss_bytes{0}; // Resident Set Size (physical memory)
    uint64_t process_vss_bytes{0}; // Virtual Set Size (virtual memory)
    uint64_t process_peak_rss_bytes{0}; // Peak RSS since start

    // GPU memory (if available)
    uint64_t gpu_memory_used_bytes{0}; // GPU memory used
    uint64_t gpu_memory_total_bytes{0}; // Total GPU memory
    double gpu_memory_utilization_percent{0.0}; // GPU memory utilization

    // Queue sizes (diagnostic)
    uint64_t pts_storage_queue_size{0}; // PTS storage queue size

    // Memory contract fallback diagnostics.
    memory_fallback_counters fallback_counters{};
};

/**
 * @brief Gets current memory statistics.
 * @return Memory statistics if available, std::nullopt if monitoring fails.
 */
[[nodiscard]] std::optional<memory_stats> get_memory_stats();

/**
 * @brief Initializes memory monitoring (must be called before using get_memory_stats).
 * @return true if initialization successful, false otherwise.
 */
[[nodiscard]] bool init_memory_monitoring();

/**
 * @brief Shuts down memory monitoring.
 */
void shutdown_memory_monitoring();

/**
 * @brief Registers a PTS storage queue size callback for one encoder instance (multi-instance safe).
 *
 * Multiple callbacks are aggregated (summed) in get_memory_stats(). Registering again with the same
 * callback_context replaces the previous entry for that context.
 */
void register_pts_storage_queue_size_callback(uint64_t (*callback)(const void *callback_context),
                                              const void *callback_context);

/**
 * @brief Removes a PTS queue size callback registered for callback_context.
 */
void unregister_pts_storage_queue_size_callback(const void *callback_context);

/**
 * @brief Registers memory-contract fallback counters for one encoder instance (multi-instance safe).
 *
 * Snapshots from all registered callbacks are summed field-wise in get_memory_stats().
 */
void register_memory_fallback_counters_callback(memory_fallback_counters (*callback)(const void *callback_context),
                                                const void *callback_context);

/**
 * @brief Removes a fallback-counters callback registered for callback_context.
 */
void unregister_memory_fallback_counters_callback(const void *callback_context);

/**
 * @brief Sets a callback function to get PTS storage queue size (avoids circular dependency).
 * @deprecated Prefer register_pts_storage_queue_size_callback(); this alias registers (same context replaces).
 */
void set_pts_storage_queue_size_callback(uint64_t (*callback)(const void *callback_context),
                                         const void *callback_context);

/**
 * @brief Sets callback used to fetch memory-contract fallback counters.
 * @deprecated Prefer register_memory_fallback_counters_callback().
 */
void set_memory_fallback_counters_callback(memory_fallback_counters (*callback)(const void *callback_context),
                                           const void *callback_context);

/**
 * @brief Sets the PTS storage queue size for diagnostics (deprecated - use callback).
 * @param queue_size Current size of PTS storage queue.
 */
void set_pts_storage_queue_size(uint64_t queue_size);

/**
 * @brief Initializes the GStreamer debug category for memory monitoring.
 */
void init_memory_monitor_debug_category();
