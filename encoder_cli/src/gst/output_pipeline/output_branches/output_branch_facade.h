// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <cstdint>

#include "../../../../libvcai/include/bitrate_calculation.h"
#include "../../../../libvcai/include/gst_raii.h"
#include "../../../../libvcai/include/thread_safe_queue.h"
#include "../../../bridge/context/metadata_contract_runtime.h"
#include "../../../bridge/context/metadata_frame_bundle.h"
#include "../../../encoder_config.h"
#include "../../../global_data_instance_runtime.h"
#include "../../../runtime/monitoring_state.h"

/**
 * @file output_branch_facade.h
 * @brief Non-owning view over the output/PRI/display slice of global_data.
 *
 * The output branch factory, branch controller, and each output branch consume
 * this narrow facade instead of the whole `global_data` aggregate, so enabling a
 * branch does not recompile or re-couple the outputs. The aggregate still owns
 * all state; this facade only exposes the members the output branches read.
 *
 * Sibling slice 05 (probes/queues/paths) builds on this facade.
 */

/**
 * @brief Forward declaration for the shared application state.
 */
struct global_data;

/**
 * @brief Non-owning view over the output/PRI/display slice of global_data.
 */
class output_branch_facade {
public:
    /**
     * @brief Binds the facade to the output branch slice of an existing aggregate.
     * @param data The aggregate that owns the state. Must outlive the facade.
     */
    explicit output_branch_facade(global_data &data);

    /**
     * @brief Returns the wrapped aggregate.
     *
     * Escape hatch for GStreamer probe/signal user_data that requires the concrete
     * aggregate (e.g. gst_pad_add_probe / g_signal_connect raw pointers).
     */
    [[nodiscard]] global_data &instance() const;

    /// Delegates the strict metadata-contract fail-stop to the aggregate.
    void request_metadata_contract_fail_stop(const char *reason) const;

    const encoder_config &params;
    sf::unique_gst_element_ptr &output_pipeline;
    sf::thread_safe_queue<metadata_frame_bundle> &display_regions_queue;
    sf::thread_safe_queue<metadata_frame_bundle> &debug_regions_queue;
    metadata_contract_runtime &metadata_runtime;
    monitoring_state &monitoring;
    output_display_branch_runtime &output_display_runtime;
    output_debug_branch_runtime &output_debug_runtime;
    // ponytail: global_data.udp_bitrate_tracker is already declared mutable, so a plain
    // reference lets branches reach it through a const facade without a mutable member.
    sf::bitrate_tracker &udp_bitrate_tracker;
    std::atomic<uint16_t> &assigned_rtp_port;
    std::atomic<uint32_t> &assigned_rtp_session_id;

private:
    global_data &data_;
};
