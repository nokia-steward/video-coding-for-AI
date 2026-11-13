// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../../libvcai/include/rectangle.h"
#include "../../encoder_config.h"

struct frame_properties;

/**
 * @file rtp_timestamp_overlay.height
 * @brief RTP timestamp extraction and overlay functionality.
 */

/**
 * @brief Initializes the RTP timestamp overlay debug category.
 */
void init_rtp_timestamp_overlay_debug_category();

/**
 * @brief Sets up RTP timestamp extraction from RTP jitterbuffer output.
 *
 * Adds a pad probe to extract RTP timestamps from caps and store them
 * for later drawing on decoded frames. The probe is placed on the jitterbuffer
 * output pad where RTP caps are still available (before depayload).
 *
 * @param jitterbuffer The RTP jitterbuffer element.
 * @param clock_rate The RTP clock rate (typically 90000).
 * @return true if pad probe was successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_rtp_timestamp_extraction(GstElement *jitterbuffer, guint32 clock_rate);

/**
 * @brief Sets up RTP timestamp attachment to decoded frame buffers.
 *
 * Adds a pad probe after the decoder to attach RTP timestamps to frame buffers
 * as metadata for later use.
 *
 * @param decoder The decoder element.
 * @return true if pad probe was successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_rtp_timestamp_attachment(GstElement *decoder);

/**
 * @brief Sets up RTP timestamp overlay drawing on frames before encoding.
 *
 * Adds a pad probe on the appsrc output (src pad) to draw RTP timestamps in the
 * lower left corner of frames before they are encoded. This ensures the timestamp
 * is drawn on CPU-accessible buffers before they are uploaded to GPU memory.
 *
 * @param encoder The appsrc element (encoding_src).
 * @param config The encoder config (draw_rtp_timestamp, pack_rois).
 * @return true if pad probe was successfully attached, false otherwise.
 */
[[nodiscard]] bool setup_rtp_timestamp_overlay(GstElement *encoder, const encoder_config &config);

/**
 * @brief Draws RTP timestamp directly on a raw frame buffer.
 *
 * This is used when ROI packing is enabled so the timestamp text is included
 * in the packed pixels rather than drawn after packing.
 *
 * @param buffer The raw frame buffer to draw on.
 * @param props Frame properties describing width, height, and format.
 * @return true if the timestamp was drawn, false otherwise.
 */
[[nodiscard]] bool draw_rtp_timestamp_on_buffer(GstBuffer *buffer, const frame_properties &props);

/**
 * @brief Creates a ROI rectangle for the RTP timestamp overlay area.
 *
 * Calculates the rectangle that covers the timestamp text area in the lower left corner.
 * The timestamp format is HH:MM:SS.mmm (12 characters).
 *
 * @param width Frame width.
 * @param height Frame height.
 * @return Rectangle covering the timestamp area.
 */
[[nodiscard]] sf::rectangle create_timestamp_roi_rectangle(int width, int height);

/**
 * @brief Clears all stored RTP timestamps.
 *
 * This should be called when resetting the pipeline or starting a new stream.
 */
void clear_rtp_timestamps();
