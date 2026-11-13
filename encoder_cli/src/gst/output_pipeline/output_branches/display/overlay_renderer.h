// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <gstnvdsmeta.h>

#include <optional>
#include <utility>

#include "../../../../global_data.h"

/**
 * @file overlay_renderer.h
 * @brief Overlay compositing for display branch frames.
 */

/**
 * @brief Resolves frame dimensions from buffer video meta, batch frame meta, or caps.
 * @param pad The pad that carried the buffer (optional, used as a caps fallback).
 * @param buffer The frame buffer.
 * @param batch_meta The NvDsBatchMeta currently attached to the buffer (may be null).
 * @return A (width, height) pair if resolvable, otherwise nullopt.
 */
[[nodiscard]] std::optional<std::pair<int, int>> overlay_frame_dimensions(GstPad *pad, GstBuffer *buffer,
                                                                          NvDsBatchMeta *batch_meta);

/**
 * @brief Prepared compositing context for one display frame.
 */
struct overlay_frame_context {
    NvDsBatchMeta *batch_meta = nullptr;
    int width = 0;
    int height = 0;
};

/**
 * @brief Prepares a frame for overlay compositing.
 *
 * Attaches/creates the NVDS batch meta, resolves frame dimensions, and clears stale display metas
 * (Jetson-vs-x86 handling) so the caller can draw FPS or bitrate overlays on a clean frame.
 *
 * @param pad The pad that carried the buffer.
 * @param buffer A writable frame buffer.
 * @param data Global data providing per-instance overlay last-frame tracking.
 * @param warned_missing_batch_meta One-shot warning flag for "batch meta missing" log.
 * @param warned_create_meta_failed One-shot warning flag for "attach batch meta failed" log.
 * @return A ready overlay_frame_context, or nullopt when compositing should be skipped for this frame.
 */
[[nodiscard]] std::optional<overlay_frame_context> prepare_overlay_frame(GstPad *pad, GstBuffer *buffer,
                                                                         global_data &data,
                                                                         bool &warned_missing_batch_meta,
                                                                         bool &warned_create_meta_failed);
