// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../roi_strategy_snapshot.h"
#include "detection_interface.h"

#include <vector>

/**
 * @file detection_factory.height
 * @brief Declares factory functions for creating and selecting detection mode contexts.
 */

/**
 * @brief Creates all detection mode contexts.
 * @return Vector of all detection mode contexts
 */
[[nodiscard]] std::vector<detection_mode_context> create_detection_mode_contexts();

/**
 * @brief Gets the active detection mode based on configuration.
 * @param snapshot Availability/config snapshot
 * @return Pointer to active detection mode context, or nullptr if none enabled
 */
[[nodiscard]] const detection_mode_context *get_active_detection_mode(const roi_strategy_snapshot &snapshot);
