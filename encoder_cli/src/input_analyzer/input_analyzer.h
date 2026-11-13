// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>

struct frame_properties;

/**
 * @file input_analyzer.height
 * @brief Adaptive luma input analyzer: predicts (downscale factor d, upscale multiplier u) from a frame.
 *
 * When enable_adaptive_luma_adaptation is true, the encoder loads the ONNX model at startup.
 * If loading fails, the application exits. During encoding, if analyze() fails for a frame,
 * the caller should reuse the previous frame's (d, u).
 */

/**
 * @brief Opaque handle for the input analyzer (ONNX session and pre/post state).
 */
struct input_analyzer;

/**
 * @brief Releases resources held by the analyzer.
 */
void input_analyzer_destroy(input_analyzer *analyzer);

using input_analyzer_ptr = std::unique_ptr<input_analyzer, void (*)(input_analyzer *)>;

/**
 * @brief Allocates an analyzer (call input_analyzer_init next).
 */
[[nodiscard]] input_analyzer_ptr input_analyzer_create();

/**
 * @brief Initializes the analyzer by loading the ONNX model from model_path.
 * @param analyzer Uninitialized analyzer (`input_analyzer_ptr::get()`).
 * @param model_path Path to the .onnx model file.
 * @return true if the model was loaded successfully, false otherwise.
 */
[[nodiscard]] bool input_analyzer_init(input_analyzer *analyzer, const std::string &model_path);

/**
 * @brief Returns true if the encoder was built with ONNX Runtime (adaptive luma inference available).
 */
[[nodiscard]] bool input_analyzer_built_with_onnx_runtime();

/**
 * @brief Returns true if the analyzer was successfully initialized and can run inference.
 */
[[nodiscard]] bool input_analyzer_is_valid(const input_analyzer *analyzer);

/**
 * @brief Runs the input analyzer on a frame and returns predicted (d, u).
 *
 * Preprocessing: frame is converted to RGB if needed, resized to 512x512, normalized
 * (OpenImages mean/std), QP and size (width/1024, height/1024) are fed as per the paper.
 *
 * @param analyzer Initialized analyzer.
 * @param buffer GStreamer buffer (I420 or NV12; will be mapped read-only).
 * @param props Frame width, height, format.
 * @param qp Encoder QP (e.g. 22--47); scaled to [0,1] for the model.
 * @return (downscale_factor d, upscale_multiplier u) or nullopt on inference failure.
 */
[[nodiscard]] std::optional<std::pair<double, double>>
input_analyzer_analyze(input_analyzer *analyzer, GstBuffer *buffer, const frame_properties &props, int qp);

/**
 * @brief Returns (downscale factor, optional upscale multiplier) for the current frame from config or analyzer.
 *
 * When adaptive luma is on, runs the analyzer (or uses cache when keyframes-only) and returns (d, u).
 * When adaptive is off, returns (luma_downscale_factor, std::nullopt). When luma adaptation is disabled,
 * returns (1.0, std::nullopt). Used by encoder backends to apply luma scaling and build EOI state.
 */
struct luma_factors_for_frame {
    double downscale_factor = 1.0;
    std::optional<double> upscale_multiplier{};
};
struct global_data;
[[nodiscard]] luma_factors_for_frame get_luma_factors_for_frame(global_data &data, GstBuffer *buffer,
                                                                const frame_properties &props);
