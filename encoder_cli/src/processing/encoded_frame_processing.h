// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "../global_data.h"

/**
 * @file encoded_frame_processing.h
 * @brief Declares the main processing loop for encoded H.265 frames.
 */

/**
 * @brief The main function for the encoded frame processing thread.
 *
 * This function runs in a loop, consuming encoded H.265 frames from a queue,
 * retrieving the corresponding SEI metadata for that frame's timestamp,
 * injecting the SEI NAL unit, and pushing the final buffer to the output pipeline.
 *
 * @param data A reference to the shared global_data struct.
 */
void process_encoded_frames(global_data &data);

/**
 * @brief Starts GstTask-driven encoded frame processing (one queue pop + inject per iteration).
 */
[[nodiscard]] bool start_encoded_frames_gst_task(global_data &data, bypass_gst_poll_slot &slot);

/**
 * @brief Stops and joins the encoded frame GstTask.
 */
void join_encoded_frames_gst_task(global_data &data, bypass_gst_poll_slot &slot);

/**
 * @brief Injects an EOI prefix SEI into an HEVC access unit when needed.
 *
 * For background-processing modes, this function inserts an EOI SEI message on
 * IRAP access units (IDR/CRA/BLA):
 * - blur mode: object-based optimization metadata for blurred background
 * - qp_map_delta mode: object-based optimization metadata describing QP-map delta usage
 * - luma adaptation mode: luma adaptation metadata derived from user-facing controls
 *
 * For non-IRAP access units, or when no supported mode is enabled, it returns
 * an unchanged copy.
 *
 * @param frame_data Raw access unit bytes.
 * @param frame_length Number of bytes in frame_data.
 * @param background_blur_enabled Whether background blur mode is active.
 * @param qp_map_delta_enabled Whether qp_map_delta mode is active.
 * @param qp_map_background_delta Background QP delta configured for qp_map_delta mode.
 * @param luma_adaptation_enabled Whether luma adaptation signaling is active.
 * @param luma_downscale_factor Luma downscale factor used by the encoder-side pre-processing.
 * @param luma_upscale_multiplier Optional decoder backscale multiplier; when absent, full backscale (1/d) is used.
 * @return Frame bytes with optional injected EOI SEI.
 */
[[nodiscard]] std::vector<uint8_t>
maybe_inject_eoi_sei(const uint8_t *frame_data, size_t frame_length, bool background_blur_enabled,
                     bool qp_map_delta_enabled, int qp_map_background_delta, bool luma_adaptation_enabled = false,
                     double luma_downscale_factor = 1.0, std::optional<double> luma_upscale_multiplier = std::nullopt);

/**
 * @brief Initializes the GStreamer debug category for encoded frame processing.
 * This function must be called before using any GST_DEBUG macros in this module.
 */
void init_encoded_frame_processing_debug_category();
