// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file udp_frame_tracking.h
 * @brief Frame-tracking pad probe setup for UDP ingress.
 */

struct udp_input_elements;
class input_ingress_facade;


/**
 * @brief Initializes the UDP frame-tracking debug category.
 */
void init_udp_frame_tracking_debug_category();

/**
 * @brief Sets up pad probes to track frame flow through the UDP input pipeline.
 *
 * Adds probes at key points to track where frames are being processed or dropped:
 * - After parser (to see if frames pass through)
 * - After decoder (to see if decoding works and for adaptive frame dropping)
 * - After converter (to see if conversion works)
 *
 * @param elements The UDP input elements to attach probes to.
 * @param data The narrow input/ingress view (configuration and FPS monitor).
 * @return true if probes were successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_udp_frame_tracking_probes(const udp_input_elements &elements, input_ingress_facade &data);
