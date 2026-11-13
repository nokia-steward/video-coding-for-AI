// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

struct encoder_config;
struct memory_policy_runtime;

/**
 * @file encoded_injection_transform_deps.h
 * @brief Explicit dependencies for encoded injection bridge transforms (no global_data coupling in the API).
 */
struct encoded_injection_transform_deps {
    memory_policy_runtime &memory_runtime;
    const encoder_config &params;
};
