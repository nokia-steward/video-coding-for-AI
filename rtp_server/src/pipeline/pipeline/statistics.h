// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../pipeline.h"

// Forward declaration
struct rtp_pipeline;

/**
 * @brief Log comprehensive pipeline statistics.
 * @param pipeline The pipeline to log statistics for.
 */
void log_pipeline_statistics(const rtp_pipeline &pipeline);

/**
 * @brief Log periodic statistics summary.
 * @param pipeline The pipeline to log statistics for.
 */
void log_periodic_statistics(const rtp_pipeline &pipeline);
