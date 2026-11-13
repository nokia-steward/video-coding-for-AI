// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <string>
#include "../pipeline.h"

// Forward declaration
struct rtp_pipeline;

/**
 * @brief Generate a unique filename for file saving.
 * @param pipeline The pipeline to generate filename for.
 * @return The generated filename.
 */
[[nodiscard]] std::string generate_filename(const rtp_pipeline &pipeline);

/**
 * @brief Resolves the root directory for debug frame outputs.
 * @param pipeline The pipeline used to resolve output paths.
 * @return Root directory where debug frame folders should be created.
 */
[[nodiscard]] std::filesystem::path get_debug_frames_root_directory(const rtp_pipeline &pipeline);

/**
 * @brief Returns the per-port debug frame directory.
 * @param pipeline The pipeline used to resolve output paths.
 * @return `<root>/debug_frames/port_<PORT>` path.
 */
[[nodiscard]] std::filesystem::path get_debug_frames_port_directory(const rtp_pipeline &pipeline);

/**
 * @brief Clean up all created elements on error.
 * @param pipeline The pipeline to clean up.
 */
void cleanup_elements(rtp_pipeline &pipeline);
