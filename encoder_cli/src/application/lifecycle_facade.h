// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

#include "../../libvcai/include/gst_raii.h"
#include "../encoder_backend/encoder_backend_view.h"
#include "../encoder_config.h"
#include "../runtime/monitoring_state.h"

/**
 * @file lifecycle_facade.h
 * @brief Non-owning view over the application-orchestration slice of `global_data`.
 *
 * The application runner and input-mode runtime loops — file, camera, and UDP
 * mode startup/shutdown and the periodic reporting coordination, plus the
 * pipeline-startup/state-management helpers — consume this narrow facade (or
 * `instance()` where a deep helper still needs the concrete aggregate) instead
 * of the whole `global_data` struct, so mode lifecycle and pipeline startup
 * logic are independent of output/tracker/ingress changes. The aggregate still
 * owns all state; this facade only exposes the members the orchestration
 * consumes. Mirrors the `input_ingress_facade` pattern from the ingress slice.
 */

/**
 * @brief Forward declaration for the shared application state.
 */
struct global_data;

/**
 * @brief Non-owning view over the application-orchestration slice of `global_data`.
 */
class lifecycle_facade {
public:
    /**
     * @brief Binds the facade to the orchestration slice of an existing aggregate.
     * @param data The aggregate that owns the state. Must outlive the facade.
     */
    explicit lifecycle_facade(global_data &data);

    /**
     * @brief Returns the wrapped aggregate.
     *
     * Escape hatch for deep seeding/processing helpers (start_worker_threads,
     * process_pipeline_messages, wait_for_webrtc_connection, bitrate calc, ...)
     * that still require the concrete aggregate.
     */
    [[nodiscard]] global_data &instance() const;

    const encoder_config &params;
    monitoring_state &monitoring;
    sf::unique_gst_element_ptr &input_pipeline;
    sf::unique_gst_element_ptr &encoding_pipeline;
    sf::unique_gst_element_ptr &output_pipeline;
    encoder_backend_view backend;
    std::vector<size_t> &pgie_class_ids_to_filter;
    std::atomic_bool &stop_processing;
    std::atomic_bool &shutdown_started;
    guint64 &num_input_frames;
    GstElement *&h265_filesink;

    /// Signals application shutdown and cleans up resources.
    void shutdown();
    /// Resets UDP-reconnect state while keeping pipelines running.
    void reset_processing_state();
    /// Requests worker stop once per lifecycle generation (close_metadata_queues).
    [[nodiscard]] bool request_worker_stop_once(bool close_metadata_queues);

private:
    global_data &data_;
};
