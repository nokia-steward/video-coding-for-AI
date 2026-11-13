// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../encoder_config.h"
#include "input_ingress_facade.h"
#include "udp_frame_tracking.h"
#include "udp_stats.h"

/**
 * @file udp_input.h
 * @brief UDP input pipeline creation and management helpers.
 */

/**
 * @brief Initializes the UDP input debug category.
 */
void init_udp_input_debug_category();

/**
 * @brief Holds the elements used for building the UDP input pipeline.
 */
struct udp_input_elements {
    GstElement *source = nullptr;
    GstElement *rtp_capsfilter = nullptr; // Capsfilter before jitterbuffer to ensure proper RTP caps
    GstElement *jitterbuffer = nullptr;
    GstElement *depay = nullptr;
    GstElement *parser = nullptr;
    GstElement *capsfilter = nullptr; // Only used for H265 byte-stream format
    GstElement *queue_before_decoder = nullptr;
    GstElement *decoder = nullptr;
    GstElement *capsfilter_after_decoder = nullptr; // Ensure NVMM output from decoder
    GstElement *system_converter = nullptr;
    GstElement *gpu_converter = nullptr;
    GstElement *capsfilter_after_converter = nullptr; // Force system memory format after nvvideoconvert
    GstElement *queue_before_tee = nullptr;
    GstElement *tee = nullptr;
};

/**
 * @brief Creates UDP input elements based on the current configuration.
 * @param config The encoder configuration (UDP parameters).
 * @param elements Output structure populated with created elements on success.
 * @return true if all required elements were created and configured, false otherwise.
 */
[[nodiscard]] bool create_udp_input_elements(const encoder_config &config, udp_input_elements &elements);

/**
 * @brief Sets up UDP packet logging via pad probe on the UDP source element.
 *
 * GStreamer's gst_element_get_static_pad and gst_pad_add_probe require non-const pointers,
 * so this function cannot take udpsrc as const even though it only attaches a probe.
 *
 * @param udpsrc The UDP source element to attach the pad probe to (non-const due to GStreamer API requirements).
 * @param data The narrow input/ingress view (UDP runtime counters and bitrate tracker).
 * @return true if pad probe was successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_udp_packet_logging(GstElement *udpsrc, input_ingress_facade &data);
