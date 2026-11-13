// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "output_roi_sequence_correlation.h"

#include "../input_types/frame_path_meta.h"

decoded_branch_roi_sequence_resolution
resolve_decoded_branch_roi_sequence_id(const GstClockTime pts, const GstBuffer *buffer,
                                       const metadata_contract_runtime &metadata_rt,
                                       const std::atomic<uint64_t> &strict_order_cursor) noexcept {
    decoded_branch_roi_sequence_resolution resolution{};
    if (pts != GST_CLOCK_TIME_NONE) {
        if (metadata_rt.lookup_output_sequence(pts, resolution.sequence_id)) {
            resolution.used_pts_map = true;
            return resolution;
        }
    }
    if (buffer != nullptr) {
        if (const auto contract = get_frame_contract_from_buffer(buffer);
            contract.has_value() && contract->sequence_id != 0U) {
            resolution.sequence_id = contract->sequence_id;
            resolution.used_frame_contract_meta = true;
            return resolution;
        }
    }
    resolution.sequence_id = strict_order_cursor.load(std::memory_order_relaxed);
    resolution.used_strict_frame_order = true;
    return resolution;
}

void advance_decoded_branch_roi_sequence_cursor(std::atomic<uint64_t> &strict_order_cursor,
                                                const uint64_t consumed_bundle_sequence_id) noexcept {
    strict_order_cursor.store(consumed_bundle_sequence_id + 1U, std::memory_order_relaxed);
}
