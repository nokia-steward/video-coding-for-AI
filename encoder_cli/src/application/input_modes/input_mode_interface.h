// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include "../../global_data.h"
#include "../lifecycle_facade.h"

/**
 * @brief Forward declaration for shared application state.
 */
struct global_data;

/**
 * @file input_mode_interface.height
 * @brief Interface for input mode implementations using std::function.
 */

/**
 * @brief Function table for input mode implementations using std::function.
 *
 * IMPORTANT: This uses std::function, NOT C-style function pointers.
 *
 * Benefits of std::function over function pointers:
 * - Lambdas with captures (eliminates need for void* user_data)
 * - Function objects and std::bind
 * - Better type safety and readability
 * - No void* user_data parameter needed
 *
 * Example usage:
 *   input_mode_functions funcs;
 *   funcs.is_enabled = [](const global_data &d) { return d.params.input_type_value == input_type::file; };
 *   funcs.run_mode = [](lifecycle_facade &f) { // run file mode
 *       // implementation here
 *   };
 */
struct input_mode_functions {
    /**
     * @brief Check if this input mode should be enabled.
     * @param data The global data structure.
     * @return true if mode should be enabled, false otherwise.
     *
     * Note: This is std::function<bool(const global_data &)>, NOT a function pointer.
     */
    std::function<bool(const global_data &)> is_enabled;

    /**
     * @brief Get the pipeline creator function for this input mode.
     * @param data The global data structure.
     * @return Pipeline creator function, or nullopt if not applicable.
     *
     * This function returns the appropriate pipeline creator based on:
     * - Whether ROI detection is enabled (inference_engine)
     * - The input type (file, camera, udp)
     *
     * Note: ROI detection pipeline handles all input types when enabled.
     */
    std::function<std::optional<std::function<bool(global_data &)>>(const global_data &)> get_pipeline_creator;

    /**
     * @brief Run the input mode execution loop.
     * @param data The narrow application-orchestration view.
     *
     * This function handles the mode-specific execution logic:
     * - File/Camera: Simple processing loop
     * - UDP: Connection monitoring, timeout handling, reconnection loop
     */
    std::function<void(lifecycle_facade &)> run_mode;

    /**
     * @brief Get the name of this input mode for logging.
     * @return Mode name string (e.g., "file", "camera", "udp").
     */
    std::function<const char *()> get_mode_name;
};

/**
 * @brief Input mode context containing function table.
 *
 * No user_data needed - lambdas can capture any needed state.
 */
struct input_mode_context {
    input_mode_functions functions;
};
