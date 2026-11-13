// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../roi_strategy_snapshot.h"
#include "processing_interface.h"

#include <vector>

/**
 * @file processing_factory.height
 * @brief Declares factory functions for creating and selecting processing mode contexts.
 */

/**
 * @brief Creates all processing mode contexts.
 * @return Vector of all processing mode contexts
 */
[[nodiscard]] std::vector<processing_mode_context> create_processing_mode_contexts();

/**
 * @brief Gets the active processing mode based on configuration.
 * @param snapshot Availability/config snapshot
 * @return Pointer to active processing mode context
 */
[[nodiscard]] const processing_mode_context *get_active_processing_mode(const roi_strategy_snapshot &snapshot);
