// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>

#include "../../../libvcai/include/thread_safe_queue.h"
#include "../../encoder_config.h"
#include "encoded_stream_ports.h"
#include "encoder_bridge_runtime.h"
#include "memory_policy_runtime.h"
#include "metadata_contract_runtime.h"
#include "metadata_frame_bundle.h"

class encoder_cli_app_host;

/**
 * @file encoded_injection_bridge_context.h
 * @brief Context wiring for encoded SEI/EOI injection orchestration.
 */

/**
 * @brief Aggregates per-instance encoded injection runtime references and counters.
 *
 * Holds narrow non-owning pointers to the specific sub-runtime slices the encoded
 * injection orchestrator reads, instead of the whole global_data aggregate.
 */
struct encoded_injection_bridge_context {
    encoded_stream_ports *ports = nullptr;
    encoded_bridge_runtime *egress_runtime = nullptr;
    metadata_contract_runtime *metadata_runtime = nullptr;
    sf::thread_safe_queue<metadata_frame_bundle> *packed_regions_queue = nullptr;
    memory_policy_runtime *memory_runtime = nullptr;
    const encoder_config *params = nullptr;
    encoder_cli_app_host *host = nullptr;
    uint64_t frame_counter = 0;
    uint64_t frame_skip = 0;
};
