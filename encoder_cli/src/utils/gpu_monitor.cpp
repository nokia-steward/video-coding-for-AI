// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "gpu_monitor.h"

#include "../../../libvcai/include/logging.h"

#if defined(HAVE_NVML) && HAVE_NVML == 1
#include <nvml.h>
#endif

#include <optional>
#include <string>

/**
 * @file gpu_monitor.cpp
 * @brief Implementation of GPU monitoring utilities using NVIDIA Management Library (NVML).
 */

/// GStreamer debug category for GPU monitoring.
GST_DEBUG_CATEGORY_STATIC(gpu_monitor_category);

void init_gpu_monitor_debug_category() {
    GST_DEBUG_CATEGORY_INIT(gpu_monitor_category, "gpu-monitor", 0, "GPU monitoring and statistics");
}

namespace {
#if defined(HAVE_NVML) && HAVE_NVML == 1
    /// Tracks whether NVML initialization has completed.
    bool gpu_monitoring_initialized = false;
    /// Cached handle to the primary GPU device.
    nvmlDevice_t gpu_device = nullptr;
#endif
} // namespace

bool init_gpu_monitoring() {
#if !defined(HAVE_NVML) || HAVE_NVML == 0
    sf::info(gpu_monitor_category, "GPU monitoring not available (NVML not compiled)");
    return false;
#else
    if (gpu_monitoring_initialized) {
        return true;
    }

    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        sf::warning(gpu_monitor_category, "Failed to initialize NVML: %s", nvmlErrorString(result));
        return false;
    }

    // Get handle to first GPU (GPU 0)
    result = nvmlDeviceGetHandleByIndex(0, &gpu_device);
    if (result != NVML_SUCCESS) {
        sf::warning(gpu_monitor_category, "Failed to get GPU device handle: %s", nvmlErrorString(result));
        nvmlShutdown();
        return false;
    }

    gpu_monitoring_initialized = true;
    sf::info(gpu_monitor_category, "GPU monitoring initialized successfully");
    return true;
#endif
}

void shutdown_gpu_monitoring() {
#if !defined(HAVE_NVML) || HAVE_NVML == 0
    return;
#else
    if (!gpu_monitoring_initialized) {
        return;
    }

    nvmlShutdown();
    gpu_monitoring_initialized = false;
    gpu_device = nullptr;
    sf::info(gpu_monitor_category, "GPU monitoring shut down");
#endif
}

/**
 * @brief Reads current GPU stats from NVML.
 * @return GPU statistics if available, otherwise nullopt.
 */
std::optional<gpu_stats> get_gpu_stats() {
#if !defined(HAVE_NVML) || HAVE_NVML == 0
    return std::nullopt;
#else
    if (!gpu_monitoring_initialized || !gpu_device) {
        return std::nullopt;
    }

    gpu_stats stats;

    // Get GPU utilization
    nvmlUtilization_t utilization;
    nvmlReturn_t result = nvmlDeviceGetUtilizationRates(gpu_device, &utilization);
    if (result == NVML_SUCCESS) {
        stats.utilization_percent = static_cast<double>(utilization.gpu);
    } else {
        sf::debug(gpu_monitor_category, "Failed to get GPU utilization: %s", nvmlErrorString(result));
    }

    // Get GPU memory information
    nvmlMemory_t memory_info;
    result = nvmlDeviceGetMemoryInfo(gpu_device, &memory_info);
    if (result == NVML_SUCCESS) {
        stats.memory_used_bytes = memory_info.used;
        stats.memory_total_bytes = memory_info.total;
    } else {
        sf::debug(gpu_monitor_category, "Failed to get GPU memory info: %s", nvmlErrorString(result));
    }

    // Get GPU temperature
    unsigned int temperature;
    result = nvmlDeviceGetTemperature(gpu_device, NVML_TEMPERATURE_GPU, &temperature);
    if (result == NVML_SUCCESS) {
        stats.temperature_celsius = static_cast<double>(temperature);
    } else {
        sf::debug(gpu_monitor_category, "Failed to get GPU temperature: %s", nvmlErrorString(result));
    }

    // Get GPU power consumption
    unsigned int power;
    result = nvmlDeviceGetPowerUsage(gpu_device, &power);
    if (result == NVML_SUCCESS) {
        stats.power_watts = power / 1000; // Convert from milliwatts to watts
    } else {
        sf::debug(gpu_monitor_category, "Failed to get GPU power usage: %s", nvmlErrorString(result));
    }

    // Get GPU clock speeds
    unsigned int graphics_clock;
    result = nvmlDeviceGetClockInfo(gpu_device, NVML_CLOCK_GRAPHICS, &graphics_clock);
    if (result == NVML_SUCCESS) {
        stats.clock_graphics_mhz = graphics_clock;
    } else {
        sf::debug(gpu_monitor_category, "Failed to get graphics clock: %s", nvmlErrorString(result));
    }

    unsigned int memory_clock;
    result = nvmlDeviceGetClockInfo(gpu_device, NVML_CLOCK_MEM, &memory_clock);
    if (result == NVML_SUCCESS) {
        stats.clock_memory_mhz = memory_clock;
    } else {
        sf::debug(gpu_monitor_category, "Failed to get memory clock: %s", nvmlErrorString(result));
    }

    return stats;
#endif
}
