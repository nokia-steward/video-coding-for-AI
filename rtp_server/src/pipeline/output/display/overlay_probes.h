#pragma once

#include <gst/gst.h>

struct rtp_pipeline;

[[nodiscard]] bool add_display_overlay_probes(rtp_pipeline *pipeline);

GstPadProbeReturn display_bitrate_overlay_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
GstPadProbeReturn display_fps_overlay_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
