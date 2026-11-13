// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <memory>
#include <optional>

#include "encoder_backend.h"

struct global_data;

/**
 * @file encoder_backend_view.h
 * @brief Non-owning view over the encoder backend, exposing only the encoding slice.
 *
 * The active encoder backend is a field of the aggregate (`active_encoder_backend`).
 * This view presents that backend through a small, typed surface so backend-selection
 * and bridge consumers can drive it without reaching into the whole `global_data`
 * object. It is non-owning and may be empty (no backend selected): the aggregate
 * still owns the backend.
 */

/**
 * @brief A non-owning view over the encoder backend.
 *
 * Exposes exactly the backend operations consumers use: querying the pipeline
 * requirement for selection, submitting an encoded frame, and signalling
 * end-of-stream. Reads and writes are forwarded to the owned backend; an absent
 * backend reports inactive and no-ops its write operations, mirroring existing
 * call-site null checks.
 */
struct encoder_backend_view {
    /**
     * @brief Construct a view over the aggregate's active encoder backend.
     * @param be Reference to the owner of the active backend (the aggregate's
     *          `active_encoder_backend` unique_ptr). The view reads the current
     *          backend through this reference, so it stays live even if the
     *          owner is populated after the view is constructed.
     */
    explicit encoder_backend_view(std::unique_ptr<encoder_backend> &be) : backend(be) {}

    encoder_backend_view(const encoder_backend_view &) = delete;
    encoder_backend_view &operator=(const encoder_backend_view &) = delete;

    /// True when an encoder backend is active.
    [[nodiscard]] bool active() const { return backend != nullptr; }

    /// True when the active backend requires the GStreamer encoding pipeline.
    [[nodiscard]] bool requires_gst_encoding_pipeline() const;

    /**
     * @brief Submit an encoded frame to the active backend.
     * @param data Aggregate state (owned by the caller) the backend reads/writes.
     * @param output_buffer The encoded frame buffer to submit.
     * @param qp_map_payload Optional QP-map payload for this frame.
     * @return True on success (or when no backend is active, matching the
     *         existing no-op path); false on a fatal push failure.
     */
    [[nodiscard]] bool submit_frame(global_data &data, sf::unique_gst_buffer_ptr output_buffer,
                                    std::optional<qp_map_frame_payload> qp_map_payload);

    /**
     * @brief Signal end-of-stream to the active backend.
     * @param data Aggregate state (owned by the caller) the backend reads/writes.
     */
    void signal_eos(global_data &data);

private:
    std::unique_ptr<encoder_backend> &backend;
};
