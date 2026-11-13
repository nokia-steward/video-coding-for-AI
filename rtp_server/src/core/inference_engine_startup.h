// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "rtp_server_config.h"

/**
 * @brief Initializes debug category for inference engine startup.
 */
void init_inference_engine_startup_debug_category();

/**
 * @brief Ensures TensorRT engine exists before accepting connections.
 * @param config Full RTP server configuration.
 * @return true if engine is ready or inference is disabled, false otherwise.
 */
[[nodiscard]] bool ensure_inference_engine_ready(const rtp_server_config &config);
