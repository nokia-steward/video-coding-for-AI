// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <optional>

/**
 * @file gpu_monitor.height
 * @brief GPU utilization and performance monitoring utilities.
 */

/**
 * @struct gpu_stats
 * @brief GPU performance statistics.
 */
struct gpu_stats {
    double utilization_percent{0.0}; // GPU utilization percentage (0-100)
    uint64_t memory_used_bytes{0}; // GPU memory used in bytes
    uint64_t memory_total_bytes{0}; // Total GPU memory in bytes
    double temperature_celsius{0.0}; // GPU temperature in Celsius
    uint32_t power_watts{0}; // GPU power consumption in watts
    uint32_t clock_graphics_mhz{0}; // Graphics clock in MHz
    uint32_t clock_memory_mhz{0}; // Memory clock in MHz
};

/**
 * @brief Gets current GPU statistics.
 * @return GPU statistics if available, std::nullopt if GPU monitoring is not available or fails.
 */
[[nodiscard]] std::optional<gpu_stats> get_gpu_stats();

/**
 * @brief Initializes GPU monitoring (must be called before using get_gpu_stats).
 * @return true if initialization successful, false otherwise.
 */
[[nodiscard]] bool init_gpu_monitoring();

/**
 * @brief Shuts down GPU monitoring.
 */
void shutdown_gpu_monitoring();

/**
 * @brief Initializes the GStreamer debug category for GPU monitoring.
 */
void init_gpu_monitor_debug_category();
