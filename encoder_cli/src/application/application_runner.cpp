// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "application_runner.h"
#include "../encoder_cli_debug.h"
#include "../global_data.h"
#include "../libvcai/include/logging.h"
#include "input_modes/input_mode_factory.h"
#include "lifecycle_facade.h"

/**
 * @file application_runner.cpp
 * @brief Implementation of the main application runner.
 */

/**
 * @brief Start processing threads and run the main application loop.
 *
 * This function creates input mode contexts, selects the appropriate input mode
 * based on configuration, and runs the selected mode's processing loop.
 *
 * @param data The global data structure containing configuration and pipelines.
 */
void run_application(global_data &data) {
    // Create and store contexts vector to ensure it lives long enough
    const auto contexts = create_input_mode_contexts();
    const auto *active_mode = select_active_input_mode(data, contexts);

    if (!active_mode) {
        sf::error(encoder_cli_main, "No input mode matched configuration");
        return;
    }

    // Route the mode's run loop through the narrow lifecycle view
    lifecycle_facade facade(data);
    active_mode->functions.run_mode(facade);
}
