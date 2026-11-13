// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../encoder_enums.h"
#include "../udp_input.h"

#include <gst/gst.h>

/**
 * @file roi_input_pipeline_internal.h
 * @brief Shared types and per-input-type factories for ROI detection ingress branches (not public API).
 */

/**
 * @brief Forward declaration for the narrow input/ingress view.
 */
class input_ingress_facade;

/**
 * @brief Holds the ingress elements in front of DeepStream `nvvidconv` for ROI detection.
 */
struct roi_input_elements {
    GstElement *input_src = nullptr;
    GstElement *input_filter = nullptr;
    GstElement *tee = nullptr;
    GstElement *queue = nullptr; ///< Queue for camera (MJPEG) path
    GstElement *jpegdec = nullptr;
    GstElement *videoconvert = nullptr;
    udp_input_elements udp_elements{};
};

/**
 * @brief Function-pointer table for file / camera / UDP ROI detection ingress assembly.
 *
 * Mirrors the dispatch style of `select_input_pipeline_creator()` in `input_pipeline.cpp`: one branch
 * per `input_type`, with create → add → link steps kept together per mode.
 */
struct roi_input_branch_factory {
    bool (*create)(input_ingress_facade &data, roi_input_elements &elements);
    void (*add_to_pipeline)(input_ingress_facade &data, const roi_input_elements &elements);
    bool (*link)(input_ingress_facade &data, const roi_input_elements &elements, GstElement *nvvidconv);
};

/**
 * @brief Returns the factory for the configured input type, or an invalid entry (null function pointers)
 *        if the type is not supported for ROI detection.
 */
[[nodiscard]] const roi_input_branch_factory &get_roi_input_branch_factory(input_type type);

/**
 * @brief UDP-only post-link setup (logging, probes, optional RTP timestamp path).
 */
[[nodiscard]] bool setup_udp_features_for_roi(input_ingress_facade &data, const udp_input_elements &udp_elements);
