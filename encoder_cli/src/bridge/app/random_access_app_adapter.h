// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../context/encoder_cli_app_host.h"

/**
 * @file random_access_app_adapter.h
 * @brief App-side adapter for random-access bridge fail-fast behavior.
 */

/**
 * @brief Handles app-owned fail-fast signaling for random-access orchestration.
 */
class random_access_app_adapter {
public:
    explicit random_access_app_adapter(encoder_cli_app_host &host_ref) : host(host_ref) {}

    void request_fail_fast_stop(const char *reason) const { host.app_request_fail_fast_stop(reason); }

private:
    encoder_cli_app_host &host;
};
