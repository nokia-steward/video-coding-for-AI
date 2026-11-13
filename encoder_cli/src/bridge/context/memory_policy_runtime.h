// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <cstdint>

/**
 * @brief Preferred memory-domain contract per pipeline stage.
 */
enum class memory_domain_contract : uint8_t {
    unknown = 0,
    system,
    nvmm,
    cuda,
    dmabuf,
    non_system_other,
};

/**
 * @brief Snapshot of memory fallback counters for reporting paths.
 */
struct memory_fallback_counters {
    uint64_t memory_domain_fallback_conversions = 0;
    uint64_t writable_buffer_fallback_conversions = 0;
    uint64_t fallback_failures = 0;
};

/**
 * @brief Per-instance runtime state for memory contract policy and counters.
 */
struct memory_policy_runtime {
    std::atomic<uint64_t> memory_domain_fallback_conversions{0};
    std::atomic<uint64_t> writable_buffer_fallback_conversions{0};
    std::atomic<uint64_t> fallback_failures{0};

    std::atomic<memory_domain_contract> roi_expected_domain{memory_domain_contract::system};
    std::atomic<memory_domain_contract> encoding_upload_expected_domain{memory_domain_contract::unknown};

    void reset() {
        memory_domain_fallback_conversions.store(0);
        writable_buffer_fallback_conversions.store(0);
        fallback_failures.store(0);
        roi_expected_domain.store(memory_domain_contract::system);
        encoding_upload_expected_domain.store(memory_domain_contract::unknown);
    }

    void record_memory_domain_fallback_conversion() { memory_domain_fallback_conversions.fetch_add(1); }

    void record_writable_buffer_fallback_conversion() { writable_buffer_fallback_conversions.fetch_add(1); }

    void record_fallback_failure() { fallback_failures.fetch_add(1); }

    [[nodiscard]] memory_fallback_counters snapshot_counters() const {
        return {
                .memory_domain_fallback_conversions = memory_domain_fallback_conversions.load(),
                .writable_buffer_fallback_conversions = writable_buffer_fallback_conversions.load(),
                .fallback_failures = fallback_failures.load(),
        };
    }
};
