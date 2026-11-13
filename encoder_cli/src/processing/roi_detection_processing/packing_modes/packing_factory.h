// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../roi_strategy_snapshot.h"
#include "packing_interface.h"

#include <vector>

/**
 * @file packing_factory.height
 * @brief Declares factory functions for creating and selecting packing mode contexts.
 */

/**
 * @brief Creates all packing mode contexts.
 * @return Vector of all packing mode contexts
 */
[[nodiscard]] std::vector<packing_mode_context> create_packing_mode_contexts();

/**
 * @brief Gets the active packing mode based on configuration.
 * @param snapshot Availability/config snapshot
 * @return Pointer to active packing mode context, or nullptr if packing disabled
 */
[[nodiscard]] const packing_mode_context *get_active_packing_mode(const roi_strategy_snapshot &snapshot);
