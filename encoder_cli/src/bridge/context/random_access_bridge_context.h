// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

struct global_data;
struct random_access_packer;

/**
 * @file random_access_bridge_context.h
 * @brief Context wiring for random-access processing bridge orchestration.
 */

/**
 * @brief Aggregates per-instance references required by random-access orchestration.
 *
 * ponytail: intentionally still routes through global_data because the random-access
 * bridge core reads params + metadata + memory + the packed/display/debug queues +
 * the app host across ~15 helpers, and its public seam is already the narrow
 * random_access_bridge_core::media_session handle (see its header). Thin these helpers
 * onto sub-runtime refs (and migrate get_luma_factors_for_frame /
 * create_metadata_and_process_frame off the aggregate) to close this last raw access.
 */
struct random_access_bridge_context {
    global_data *data = nullptr;
    random_access_packer *packer = nullptr;
};
