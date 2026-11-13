// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "queues.h"
#include "../../../libvcai/include/logging.h"
#include "../../encoder_enums.h"
#include "elements.h"
#include "output_pipeline_debug.h"

#include <gst/gst.h>

/**
 * @file queues.cpp
 * @brief Implementation of output pipeline queue configuration.
 */

namespace {
    /**
     * @brief Calculates queue size based on framerate.
     * @param framerate The framerate value (0 means use default).
     * @return Queue size as 2 * framerate, or 120 if framerate is 0.
     */
    int calculate_queue_size(int framerate) {
        const int effective_framerate = framerate > 0 ? framerate : 60;
        return 2 * effective_framerate;
    }

    /**
     * @brief Configures display branch queue.
     * @param elements The pipeline elements.
     * @param config The encoder config (packer type).
     */
    void configure_display_queue(const output_pipeline_elements &elements, const encoder_config &config) {
        if (elements.queue_display) {
            // Uniform queue size for synchronization across all branches
            const int max_buffers = calculate_queue_size(config.framerate);
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_display, "max-size-buffers", max_buffers, "max-size-time", 3000 * GST_MSECOND,
                         "leaky", leaky_mode, nullptr);
            sf::debug(output_pipeline,
                      "Configured display branch queue: max-size-buffers=%d, max-size-time=3000ms, leaky=%d "
                      "(packer_type=%s)",
                      max_buffers, leaky_mode, packer_type_to_string(config.packer_type_value));
        }
    }

    /**
     * @brief Configures RTP branch queue.
     * @param elements The pipeline elements.
     * @param config The encoder config (framerate).
     */
    void configure_rtp_queue(const output_pipeline_elements &elements, const encoder_config &config) {
        if (elements.queue_rtp) {
            // Uniform queue size for synchronization across all branches
            const int max_buffers = calculate_queue_size(config.framerate);
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_rtp, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured RTP branch queue: max-size-buffers=%d, leaky=no_leak", max_buffers);
        }
    }

    /**
     * @brief Configures H.265 file branch queue.
     * @param elements The pipeline elements.
     * @param config The encoder config (framerate).
     */
    void configure_h265_file_queue(const output_pipeline_elements &elements, const encoder_config &config) {
        if (elements.queue_file) {
            // Uniform queue size for synchronization across all branches
            const int max_buffers = calculate_queue_size(config.framerate);
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_file, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured H.265 file branch queue: max-size-buffers=%d, leaky=no_leak",
                      max_buffers);
        }
    }

    /**
     * @brief Configures MP4 branch queue.
     * @param elements The pipeline elements.
     * @param config The encoder config (framerate).
     */
    void configure_mp4_queue(const output_pipeline_elements &elements, const encoder_config &config) {
        if (elements.queue_mp4) {
            // Uniform queue size for synchronization across all branches
            const int max_buffers = calculate_queue_size(config.framerate);
            const int leaky_mode = static_cast<int>(queue_leaky_mode::no_leak);
            g_object_set(elements.queue_mp4, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured MP4 branch queue: max-size-buffers=%d, leaky=no_leak", max_buffers);
        }
    }

    /**
     * @brief Configures debug branch queue.
     * @param elements The pipeline elements.
     * @param config The encoder config (framerate).
     */
    void configure_debug_queue(const output_pipeline_elements &elements, const encoder_config &config) {
        if (elements.queue_debug) {
            // The debug branch has a heavyweight decode + PNG-encode chain.
            // Using leak_downstream prevents it from back-pressuring the tee
            // and deadlocking the output pipeline.
            const int max_buffers = calculate_queue_size(config.framerate);
            const int leaky_mode = static_cast<int>(queue_leaky_mode::leak_downstream);
            g_object_set(elements.queue_debug, "max-size-buffers", max_buffers, "max-size-time", 0, "leaky", leaky_mode,
                         nullptr);
            sf::debug(output_pipeline, "Configured debug branch queue: max-size-buffers=%d, leaky=downstream",
                      max_buffers);
        }
    }
} // namespace

void configure_output_queues(const output_pipeline_elements &elements, const encoder_config &config) {
    configure_display_queue(elements, config);
    configure_rtp_queue(elements, config);
    configure_h265_file_queue(elements, config);
    configure_mp4_queue(elements, config);
    configure_debug_queue(elements, config);
}

void configure_display_sink(const output_pipeline_elements &elements, const encoder_config &config) {
    // In random access mode, frames arrive in batches from the packer.
    // With sync=true, frames with timestamps in the past relative to the clock get dropped.
    // Use sync=false to display frames immediately as they're decoded, preventing frame drops.
    const bool enable_sync = false; // Always false to prevent dropping batched frames

    g_object_set(elements.sink_display, "sync", enable_sync, "qos", false, "enable-last-sample", false, "async", false,
                 nullptr);
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
    sf::info(output_pipeline,
             "Configured display sink: sync=%s, qos=false, enable-last-sample=false (Jetson/ARM64, packer_type=%s)",
             enable_sync ? "true" : "false", packer_type_to_string(config.packer_type_value));
#else
    if (elements.converter_display) {
        // Use NVMM buffers for non-ARM64 display path
        g_object_set(elements.converter_display, "nvbuf-memory-type", 3, nullptr);
    }
    sf::info(output_pipeline, "Configured display sink: sync=%s, qos=false, enable-last-sample=false (packer_type=%s)",
             enable_sync ? "true" : "false", packer_type_to_string(config.packer_type_value));
#endif
}
