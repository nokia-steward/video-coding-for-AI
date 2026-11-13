// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <string>

#include "../../encoder_config.h"
#include "input_ingress_facade.h"

/**
 * @file common_input.h
 * @brief Common utilities and shared elements for input pipelines.
 */

/**
 * @brief Initializes the common input debug category.
 */
void init_common_input_debug_category();

/**
 * @brief Shared sink-side elements for input pipelines.
 */
struct input_sink_branch_elements {
    GstElement *appsink = nullptr;
    GstElement *queue_original = nullptr;
    GstElement *converter_original = nullptr;
    GstElement *encoder_original = nullptr;
    GstElement *sink_original = nullptr;
};

/**
 * @brief Creates common elements used in all input pipelines.
 * @param data The global data structure.
 * @param filesrc Output parameter for the filesrc element.
 * @param parser Output parameter for the parser element.
 * @param tee Output parameter for the tee element.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_common_input_elements(input_ingress_facade &data, GstElement *&filesrc, GstElement *&parser,
                                                GstElement *&tee);

/**
 * @brief Creates original frame saving elements if enabled.
 * @param data The encoder config.
 * @param model_name The model name for creating model-specific directories.
 * @param queue_original Output parameter for the queue element.
 * @param converter_original Output parameter for the converter element.
 * @param encoder_original Output parameter for the encoder element.
 * @param sink_original Output parameter for the sink element.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_original_frame_saving_elements(const encoder_config &data, const std::string &model_name,
                                                         GstElement *&queue_original, GstElement *&converter_original,
                                                         GstElement *&encoder_original, GstElement *&sink_original);

/**
 * @brief Creates and configures appsink plus optional original-frame branch elements.
 * @param data The input/ingress runtime slice.
 * @param appsink_name Name for the appsink element.
 * @param pipeline_label Human-readable pipeline label for diagnostics.
 * @param sink_elements Output sink element bundle.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_input_sink_branch_elements(input_ingress_facade &data, const char *appsink_name,
                                                     const char *pipeline_label,
                                                     input_sink_branch_elements &sink_elements);

/**
 * @brief Configures and connects an appsink element.
 * @param appsink The appsink element to configure.
 * @param data The input/ingress runtime slice.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool configure_appsink(GstElement *appsink, input_ingress_facade &data);

/**
 * @brief Links original frame saving branch to tee if enabled.
 * @param tee The tee element.
 * @param queue_original The queue element for original frame saving.
 * @param converter_original The converter element for original frame saving.
 * @param encoder_original The encoder element for original frame saving.
 * @param sink_original The sink element for original frame saving.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool link_original_frame_saving_branch(GstElement *tee, GstElement *queue_original,
                                                     GstElement *converter_original, GstElement *encoder_original,
                                                     GstElement *sink_original);
