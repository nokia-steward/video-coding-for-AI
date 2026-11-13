// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "ingress_bridge_runtime.h"

/**
 * @file ingress_bridge_context.h
 * @brief Ingress bridge context for callback and polling orchestration.
 */

/**
 * @brief Aggregates ingress runtime state for one encoder instance.
 */
struct ingress_bridge_context {
    ingress_bridge_runtime *runtime = nullptr;
};
