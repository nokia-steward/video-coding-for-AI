// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../context/encoder_cli_app_host.h"

/**
 * @file encoder_app_adapter.h
 * @brief App-side adapter for non-media concerns on encoder egress.
 */

/**
 * @brief Handles status reporting and fail-fast signaling for egress bridge code.
 */
class encoder_app_adapter {
public:
    explicit encoder_app_adapter(encoder_cli_app_host &host_ref) : host(host_ref) {}

    void record_output_update() const { host.enc_record_output_update(); }

    void record_sei_injection(const guint64 sei_count) const { host.enc_record_sei_injection(sei_count); }

    void record_encoder_submission() const { host.enc_record_encoder_submission(); }

    [[nodiscard]] bool rollback_latest_pending_luma_state() const {
        return host.enc_rollback_latest_pending_luma_state();
    }

    void request_fail_fast_stop(const char *reason) const { host.app_request_fail_fast_stop(reason); }

private:
    encoder_cli_app_host &host;
};
