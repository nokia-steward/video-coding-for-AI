// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../application/lifecycle_facade.h"

/**
 * @file startup.height
 * @brief Declares functions for pipeline creation and startup.
 */

/**
 * @brief Create all required pipelines for the application.
 * @param data The pipeline-runtime facade.
 * @return true if all pipelines were created successfully, false otherwise.
 */
[[nodiscard]] bool create_all_pipelines(lifecycle_facade &data);

/**
 * @brief Start all pipelines by setting them to playing state.
 * For WebRTC mode, input and encoding pipelines are not started here.
 * They should be started after WebRTC connection is established.
 * @param data The pipeline-runtime facade.
 */
void start_all_pipelines(lifecycle_facade &data);

/**
 * @brief Starts input and encoding pipelines (sets them to PLAYING state).
 * This function is used to start input/encoding pipelines after WebRTC connection is established.
 * @param data The pipeline-runtime facade.
 * @return true if pipelines were started successfully, false otherwise.
 */
[[nodiscard]] bool start_input_and_encoding_pipelines(lifecycle_facade &data);

/**
 * @brief Starts only the input pipeline (sets it to PLAYING) and, when DeepStream bypass polling is
 * enabled, restarts the bypass polling threads.
 *
 * File mode defers the input pipeline start until after the consuming worker threads are armed, so
 * the runner calls this helper once those workers are ready to avoid EOS-closing a finite clip before
 * ROI/encoder-submission workers run.
 * @param data The pipeline-runtime facade.
 * @return true if the input pipeline was started successfully, false otherwise.
 */
[[nodiscard]] bool start_input_pipeline_only(lifecycle_facade &data);

/**
 * @brief Configure DeepStream elements if ROI detection is enabled.
 * @param data The pipeline-runtime facade.
 */
void configure_deepstream_elements(lifecycle_facade &data);
