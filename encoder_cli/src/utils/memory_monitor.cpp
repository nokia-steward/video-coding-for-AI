// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "memory_monitor.h"

#include "../../../libvcai/include/logging.h"
#include "../utils/gpu_monitor.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

/**
 * @file memory_monitor.cpp
 * @brief Implementation of memory monitoring utilities.
 */

/// GStreamer debug category for memory monitoring.
GST_DEBUG_CATEGORY_STATIC(memory_monitor_category);

void init_memory_monitor_debug_category() {
    GST_DEBUG_CATEGORY_INIT(memory_monitor_category, "memory-monitor", 0, "Memory monitoring and diagnostics");
}

namespace {
    /// Tracks whether memory monitoring has been initialized.
    bool memory_monitoring_initialized = false;

    struct pts_callback_entry {
        uint64_t (*callback)(const void *callback_context);
        const void *context;
    };

    struct fallback_callback_entry {
        memory_fallback_counters (*callback)(const void *callback_context);
        const void *context;
    };

    std::mutex callback_mutex;
    std::vector<pts_callback_entry> pts_callbacks;
    std::vector<fallback_callback_entry> fallback_callbacks;

    void remove_pts_by_context(const void *context) {
        pts_callbacks.erase(std::remove_if(pts_callbacks.begin(), pts_callbacks.end(),
                                           [context](const pts_callback_entry &e) { return e.context == context; }),
                            pts_callbacks.end());
    }

    void remove_fallback_by_context(const void *context) {
        fallback_callbacks.erase(
                std::remove_if(fallback_callbacks.begin(), fallback_callbacks.end(),
                               [context](const fallback_callback_entry &e) { return e.context == context; }),
                fallback_callbacks.end());
    }

    void register_or_replace_pts(uint64_t (*callback)(const void *callback_context), const void *callback_context) {
        std::lock_guard lock(callback_mutex);
        remove_pts_by_context(callback_context);
        pts_callbacks.push_back(pts_callback_entry{.callback = callback, .context = callback_context});
    }

    void register_or_replace_fallback(memory_fallback_counters (*callback)(const void *callback_context),
                                      const void *callback_context) {
        std::lock_guard lock(callback_mutex);
        remove_fallback_by_context(callback_context);
        fallback_callbacks.push_back(fallback_callback_entry{.callback = callback, .context = callback_context});
    }

    void clear_all_callbacks() {
        std::lock_guard lock(callback_mutex);
        pts_callbacks.clear();
        fallback_callbacks.clear();
    }
} // namespace

bool init_memory_monitoring() {
    if (memory_monitoring_initialized) {
        return true;
    }

    memory_monitoring_initialized = true;
    sf::info(memory_monitor_category, "Memory monitoring initialized");
    return true;
}

void shutdown_memory_monitoring() {
    if (!memory_monitoring_initialized) {
        return;
    }

    clear_all_callbacks();
    memory_monitoring_initialized = false;
    sf::info(memory_monitor_category, "Memory monitoring shut down");
}

void register_pts_storage_queue_size_callback(uint64_t (*callback)(const void *callback_context),
                                              const void *callback_context) {
    register_or_replace_pts(callback, callback_context);
}

void unregister_pts_storage_queue_size_callback(const void *callback_context) {
    std::lock_guard lock(callback_mutex);
    remove_pts_by_context(callback_context);
}

void register_memory_fallback_counters_callback(memory_fallback_counters (*callback)(const void *callback_context),
                                                const void *callback_context) {
    register_or_replace_fallback(callback, callback_context);
}

void unregister_memory_fallback_counters_callback(const void *callback_context) {
    std::lock_guard lock(callback_mutex);
    remove_fallback_by_context(callback_context);
}

void set_pts_storage_queue_size_callback(uint64_t (*callback)(const void *callback_context),
                                         const void *callback_context) {
    register_pts_storage_queue_size_callback(callback, callback_context);
}

void set_memory_fallback_counters_callback(memory_fallback_counters (*callback)(const void *callback_context),
                                           const void *callback_context) {
    register_memory_fallback_counters_callback(callback, callback_context);
}

void set_pts_storage_queue_size([[maybe_unused]] const uint64_t queue_size) {
    // Deprecated: Use set_pts_storage_queue_size_callback() instead
    // This function is kept for API compatibility but does nothing
}

/**
 * @brief Reads process memory statistics from /proc/self/status.
 */
[[nodiscard]] static std::optional<std::pair<uint64_t, uint64_t>> read_process_memory() {
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return std::nullopt;
    }

    uint64_t rss_kb = 0;
    uint64_t vss_kb = 0;
    std::string line;

    while (std::getline(status_file, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream iss(line);
            std::string key;
            iss >> key >> rss_kb;
        } else if (line.find("VmSize:") == 0) {
            std::istringstream iss(line);
            std::string key;
            iss >> key >> vss_kb;
        }
    }

    if (rss_kb == 0 && vss_kb == 0) {
        return std::nullopt;
    }

    return std::make_pair(rss_kb * 1024ULL, vss_kb * 1024ULL);
}

/**
 * @brief Gets peak RSS from /proc/self/status.
 */
[[nodiscard]] static uint64_t read_peak_rss() {
    std::ifstream status_file("/proc/self/status");
    if (!status_file.is_open()) {
        return 0;
    }

    uint64_t peak_rss_kb = 0;
    std::string line;

    while (std::getline(status_file, line)) {
        if (line.find("VmHWM:") == 0) {
            std::istringstream iss(line);
            std::string key;
            iss >> key >> peak_rss_kb;
            break;
        }
    }

    return peak_rss_kb * 1024ULL;
}

/**
 * @brief Collects current host/GPU memory statistics.
 * @return Memory stats if monitoring is initialized, otherwise nullopt.
 */
std::optional<memory_stats> get_memory_stats() {
    if (!memory_monitoring_initialized) {
        return std::nullopt;
    }

    memory_stats stats;

    // Read host memory
    const auto host_mem = read_process_memory();
    if (host_mem) {
        stats.process_rss_bytes = host_mem->first;
        stats.process_vss_bytes = host_mem->second;
        stats.process_peak_rss_bytes = read_peak_rss();
    }

    // Read GPU memory (if available)
    const auto gpu_stats_opt = get_gpu_stats();
    if (gpu_stats_opt) {
        const auto &gpu_stats = *gpu_stats_opt;
        stats.gpu_memory_used_bytes = gpu_stats.memory_used_bytes;
        stats.gpu_memory_total_bytes = gpu_stats.memory_total_bytes;
        if (gpu_stats.memory_total_bytes > 0) {
            stats.gpu_memory_utilization_percent = (static_cast<double>(gpu_stats.memory_used_bytes) /
                                                    static_cast<double>(gpu_stats.memory_total_bytes)) *
                                                   100.0;
        }
    }

    std::vector<pts_callback_entry> pts_copy;
    std::vector<fallback_callback_entry> fallback_copy;
    {
        std::lock_guard lock(callback_mutex);
        pts_copy = pts_callbacks;
        fallback_copy = fallback_callbacks;
    }

    uint64_t pts_sum = 0;
    for (const auto &entry: pts_copy) {
        if (entry.callback != nullptr) {
            pts_sum += entry.callback(entry.context);
        }
    }
    stats.pts_storage_queue_size = pts_sum;

    memory_fallback_counters merged{};
    for (const auto &entry: fallback_copy) {
        if (entry.callback != nullptr) {
            const auto snap = entry.callback(entry.context);
            merged.memory_domain_fallback_conversions += snap.memory_domain_fallback_conversions;
            merged.writable_buffer_fallback_conversions += snap.writable_buffer_fallback_conversions;
            merged.fallback_failures += snap.fallback_failures;
        }
    }
    stats.fallback_counters = merged;

    return stats;
}
