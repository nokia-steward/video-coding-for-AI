// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../pipeline.h"

// Forward declaration
struct rtp_pipeline;

/**
 * @brief Link elements in the transport input chain (ends at queue_encoded after byte-stream caps).
 * @param pipeline The pipeline to link.
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool link_input_pipeline(rtp_pipeline &pipeline);

/**
 * @brief Link decode chain and output branches inside the main pipeline.
 *
 * Chains queue_encoded through VCAI processing, queue_decoded, capsfilter, GPU convert, tee, and branches.
 * @param pipeline The pipeline to link.
 * @return true if successful, false otherwise.
 */
[[nodiscard]] bool link_decode_and_output_pipeline(rtp_pipeline &pipeline);
