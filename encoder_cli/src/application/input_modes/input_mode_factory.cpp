// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "input_mode_factory.h"
#include "../../encoder_cli_debug.h"
#include "../../libvcai/include/logging.h"
#include "camera/camera_input_mode.h"
#include "file/file_input_mode.h"
#include "udp/udp_input_mode.h"

#include <vector>

/**
 * @file input_mode_factory.cpp
 * @brief Implementation of input mode factory functions.
 */

/**
 * @brief Creates all input mode contexts.
 *
 * Creates contexts for file, camera, and UDP input modes.
 *
 * @return Vector of input mode contexts.
 */
std::vector<input_mode_context> create_input_mode_contexts() {
    std::vector<input_mode_context> contexts;

    contexts.push_back(create_file_input_mode_context());
    contexts.push_back(create_camera_input_mode_context());
    contexts.push_back(create_udp_input_mode_context());

    return contexts;
}

/**
 * @brief Selects the active input mode based on configuration.
 *
 * Iterates through available input mode contexts and returns the first one
 * that matches the current configuration.
 *
 * @param data The global data structure.
 * @param contexts All available input mode contexts.
 * @return Pointer to active input mode context, or nullptr if none match.
 */
const input_mode_context *select_active_input_mode(const global_data &data,
                                                   const std::vector<input_mode_context> &contexts) {
    for (const auto &context: contexts) {
        if (context.functions.is_enabled(data)) {
            sf::info(encoder_cli_main, "Selected input mode: %s", context.functions.get_mode_name());
            return &context;
        }
    }

    sf::error(encoder_cli_main, "No input mode matched configuration (input_type=%s, inference_engine=%s)",
              input_type_to_string(data.params.input_type_value), data.params.inference_engine ? "true" : "false");
    return nullptr;
}
