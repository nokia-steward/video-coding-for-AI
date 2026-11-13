// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <cstdint>
#include <gst/gst.h>
#include <mutex>
#include <unordered_map>

/**
 * @brief Snapshot of strict metadata contract counters for reporting.
 */
struct metadata_contract_counters {
    uint64_t bundles_enqueued = 0;
    uint64_t bundles_dequeued = 0;
    uint64_t sequence_mismatches = 0;
    uint64_t metadata_validation_failures = 0;
    uint64_t contract_fail_stops = 0;
};

/**
 * @brief Per-instance runtime state for strict metadata contract enforcement.
 */
struct metadata_contract_runtime {
    std::atomic_uint64_t next_sequence_to_assign{1};
    std::atomic_uint64_t expected_raw_sequence{1};
    std::atomic_uint64_t expected_encoded_sequence{1};
    std::atomic_uint64_t expected_display_sequence{1};
    std::atomic_uint64_t expected_debug_sequence{1};

    std::atomic_uint64_t bundles_enqueued{0};
    std::atomic_uint64_t bundles_dequeued{0};
    std::atomic_uint64_t sequence_mismatches{0};
    std::atomic_uint64_t metadata_validation_failures{0};
    std::atomic_uint64_t contract_fail_stops{0};

    mutable std::mutex output_sequence_by_pts_mutex{};
    std::unordered_map<GstClockTime, uint64_t> output_sequence_by_pts{};

    [[nodiscard]] uint64_t next_sequence_id() { return next_sequence_to_assign.fetch_add(1); }

    [[nodiscard]] bool validate_and_advance(std::atomic_uint64_t &expected_sequence, const uint64_t observed_sequence) {
        const uint64_t expected = expected_sequence.load();
        if (expected != observed_sequence) {
            sequence_mismatches.fetch_add(1);
            return false;
        }
        expected_sequence.fetch_add(1);
        return true;
    }

    void record_bundle_enqueued() { bundles_enqueued.fetch_add(1); }
    void record_bundle_dequeued() { bundles_dequeued.fetch_add(1); }
    void record_validation_failure() { metadata_validation_failures.fetch_add(1); }
    void record_fail_stop() { contract_fail_stops.fetch_add(1); }

    void register_output_sequence(const GstClockTime pts, const uint64_t sequence_id) {
        if (pts == GST_CLOCK_TIME_NONE) {
            return;
        }
        std::lock_guard lock(output_sequence_by_pts_mutex);
        output_sequence_by_pts[pts] = sequence_id;
        if (output_sequence_by_pts.size() > 8192U) {
            output_sequence_by_pts.clear();
        }
    }

    [[nodiscard]] bool lookup_output_sequence(const GstClockTime pts, uint64_t &sequence_id_out) const {
        if (pts == GST_CLOCK_TIME_NONE) {
            return false;
        }
        std::lock_guard lock(output_sequence_by_pts_mutex);
        const auto it = output_sequence_by_pts.find(pts);
        if (it == output_sequence_by_pts.end()) {
            return false;
        }
        sequence_id_out = it->second;
        return true;
    }

    bool unregister_output_sequence(const GstClockTime pts, const uint64_t sequence_id) {
        if (pts == GST_CLOCK_TIME_NONE) {
            return false;
        }
        std::lock_guard lock(output_sequence_by_pts_mutex);
        const auto it = output_sequence_by_pts.find(pts);
        if (it == output_sequence_by_pts.end() || it->second != sequence_id) {
            return false;
        }
        output_sequence_by_pts.erase(it);
        return true;
    }

    [[nodiscard]] metadata_contract_counters snapshot_counters() const {
        return {
                .bundles_enqueued = bundles_enqueued.load(),
                .bundles_dequeued = bundles_dequeued.load(),
                .sequence_mismatches = sequence_mismatches.load(),
                .metadata_validation_failures = metadata_validation_failures.load(),
                .contract_fail_stops = contract_fail_stops.load(),
        };
    }

    void reset() {
        next_sequence_to_assign.store(1);
        expected_raw_sequence.store(1);
        expected_encoded_sequence.store(1);
        expected_display_sequence.store(1);
        expected_debug_sequence.store(1);
        bundles_enqueued.store(0);
        bundles_dequeued.store(0);
        sequence_mismatches.store(0);
        metadata_validation_failures.store(0);
        contract_fail_stops.store(0);
        std::lock_guard lock(output_sequence_by_pts_mutex);
        output_sequence_by_pts.clear();
    }
};
