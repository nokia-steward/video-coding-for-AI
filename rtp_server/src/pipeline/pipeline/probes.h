// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include <gst/gst.h>

// Forward declaration
struct rtp_pipeline;

/**
 * @brief Pad probe callback for RTP metadata extraction.
 * @param pad The pad that triggered the probe.
 * @param info Probe information containing the buffer.
 * @param user_data User data (pointer to rtp_pipeline).
 * @return GST_PAD_PROBE_OK to continue processing.
 */
GstPadProbeReturn rtp_metadata_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

/**
 * @brief Pad probe callback for monitoring data flow.
 * @param pad The pad being probed.
 * @param info Probe information.
 * @param user_data Pointer to rtp_pipeline.
 * @return GST_PAD_PROBE_OK to continue processing.
 */
GstPadProbeReturn pad_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

/**
 * @brief Add pad probes to monitor data flow through the pipeline.
 * @param pipeline The pipeline to add probes to.
 */
void add_pad_probes(rtp_pipeline &pipeline);
