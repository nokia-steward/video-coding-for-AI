// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gst/gst.h>
#include <optional>

#include "../../../../libvcai/include/bitrate_calculation.h"
#include "../../../../libvcai/include/gst_raii.h"
#include "../../bridge/context/ingress_bridge_runtime.h"
#include "../../bridge/context/memory_policy_runtime.h"
#include "../../encoder_config.h"
#include "../../global_data_instance_runtime.h"
#include "../../runtime/bypass_state.h"
#include "../../runtime/monitoring_state.h"
#include "frame_drop_manager.h"

/**
 * @file input_ingress_facade.h
 * @brief Non-owning view over the input/ingress slice of `global_data`.
 *
 * The ingress sources — UDP, camera, file, and ROI inputs, appsink polling,
 * frame router/merger/drop, the bypass path, and the RTP timestamp overlay —
 * consume this narrow facade (or an even narrower parameter) instead of the
 * whole `global_data` aggregate, so ingress reconnect/state logic is testable
 * in isolation and independent of output or tracker changes. The aggregate
 * still owns all state; this facade only exposes the members the ingress
 * consumers read. Mirrors the `output_branch_facade` pattern from the output
 * slices.
 */

/**
 * @brief Forward declaration for the shared application state.
 */
struct global_data;

/**
 * @brief Non-owning view over the input/ingress slice of `global_data`.
 */
class input_ingress_facade {
public:
    /**
     * @brief Binds the facade to the input/ingress slice of an existing aggregate.
     * @param data The aggregate that owns the state. Must outlive the facade.
     */
    explicit input_ingress_facade(global_data &data);

    /**
     * @brief Returns the wrapped aggregate.
     *
     * Escape hatch for GStreamer probe/signal user_data that requires the concrete
     * aggregate (e.g. gst_pad_add_probe / g_signal_connect raw pointers).
     */
    [[nodiscard]] global_data &instance() const;

    /// Delegates raw-sample ingress queueing decisions to the aggregate (via the ingress app host).
    [[nodiscard]] bool should_process_raw_sample() const;

    const encoder_config &params;
    sf::unique_gst_element_ptr &input_pipeline;
    ingress_bridge_runtime &ingress_runtime;
    monitoring_state &monitoring;
    udp_input_runtime_state &udp_input_runtime;
    frame_drop_manager &udp_frame_drop_manager;
    bypass_state &bypass;
    memory_policy_runtime &memory_runtime;
    sf::bitrate_tracker &udp_bitrate_tracker;
    std::atomic_bool &stop_processing;
    std::optional<guint64> &expected_file_input_frames;
    std::atomic_bool &file_input_forced_raw_eos;
    GstElement *&raw_video_parser;

private:
    global_data &data_;
};
