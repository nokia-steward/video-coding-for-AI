// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../bridge/context/metadata_contract_runtime.h"

#include <atomic>
#include <cstdint>
#include <gst/gst.h>

/**
 * @file output_roi_sequence_correlation.h
 * @brief Resolves strict contract sequence ids for decoded output-branch buffers when PTS differs from the
 *        encoded-branch registration domain (common after H.265 parse/decode).
 */

/**
 * @brief How a decoded-branch buffer was correlated to a metadata_frame_bundle.sequence_id.
 */
struct decoded_branch_roi_sequence_resolution {
    uint64_t sequence_id = 0;
    bool used_pts_map = false;
    bool used_frame_contract_meta = false;
    bool used_strict_frame_order = false;
};

/**
 * @brief Resolves sequence id for display/debug ROI border probes.
 *
 * Preference order: exact PTS map (encoded-branch registration), frame_path_meta contract on the buffer, then strict
 * monotonic order using strict_order_cursor (must be advanced after each successful bundle dequeue).
 */
[[nodiscard]] decoded_branch_roi_sequence_resolution
resolve_decoded_branch_roi_sequence_id(GstClockTime pts, const GstBuffer *buffer,
                                       const metadata_contract_runtime &metadata_rt,
                                       const std::atomic<uint64_t> &strict_order_cursor) noexcept;

/**
 * @brief Updates strict-order cursor after dequeuing the metadata bundle for this frame.
 */
void advance_decoded_branch_roi_sequence_cursor(std::atomic<uint64_t> &strict_order_cursor,
                                                uint64_t consumed_bundle_sequence_id) noexcept;
