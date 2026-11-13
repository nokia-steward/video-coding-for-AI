// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>
#include <gst/gst.h>

#include "../../roi/random_access_packer.h"

struct global_data;

namespace random_access_bridge_core {
    /**
     * @brief Opaque session handle for random-access bridge core (implemented by global_data today).
     *
     * Keeps the public core API free of a direct global_data parameter while preserving a single
     * forward declaration in this header.
     */
    struct media_session {
        explicit media_session(global_data &runtime) : impl(runtime) {}

        global_data &impl;

        media_session(const media_session &) = delete;
        media_session &operator=(const media_session &) = delete;
        media_session(media_session &&) = delete;
        media_session &operator=(media_session &&) = delete;
    };

    /**
     * @brief Step status for random-access bridge operations.
     */
    enum class step_status { ok, recoverable_drop, fatal_stop, drain_complete };

    /**
     * @brief Step result for random-access bridge operations.
     */
    struct step_result {
        step_status status = step_status::ok;
        GstFlowReturn flow_return = GST_FLOW_OK;
    };

    /**
     * @brief Processes one complete random-access period.
     */
    [[nodiscard]] step_result process_period(media_session &session,
                                             const random_access_packer::complete_period &period,
                                             GstClockTime frame_duration, GstClockTime &next_pts,
                                             std::chrono::steady_clock::time_point &last_push_time);

    /**
     * @brief Performs random-access cleanup and encoding EOS handoff.
     */
    [[nodiscard]] step_result cleanup(media_session &session, random_access_packer *packer);
} // namespace random_access_bridge_core
