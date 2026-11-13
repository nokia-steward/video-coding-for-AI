// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>

#include "gst_raii.h"

// Forward declarations
struct rtp_pipeline;

/**
 * @brief Disconnects all signal handlers from pipeline elements.
 * @param pipeline The pipeline.
 */
void disconnect_signal_handlers(rtp_pipeline &pipeline);

/**
 * @brief Stops a single pipeline element (sets to nullptr state).
 * @param pipeline_element The pipeline element to stop.
 */
void stop_single_pipeline(sf::unique_gst_element_ptr &pipeline_element);

/**
 * @brief Stops the main pipeline (sets to nullptr state).
 * @param pipeline The pipeline.
 */
void stop_all_pipelines(rtp_pipeline &pipeline);
