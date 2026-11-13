// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file elements.height
 * @brief Defines the structure holding all output pipeline GStreamer elements.
 */

/**
 * @brief Structure to hold all output pipeline elements.
 */
struct output_pipeline_elements {
    GstElement *tee = nullptr;
    GstElement *queue_display = nullptr;
    GstElement *parser_display = nullptr;
    GstElement *decoder_display = nullptr;
    /// When encoder is kvazaarenc, display branch uses avdec_h265; videoconvert bridges to nvvideoconvert.
    GstElement *videoconvert_display_before_nvmm = nullptr;
    GstElement *converter_display_to_nvmm = nullptr;
    GstElement *caps_display_to_nvmm = nullptr;
    GstElement *streammux_display = nullptr;
    GstElement *converter_display = nullptr;
    GstElement *caps_display = nullptr;
    GstElement *transform_display = nullptr; // nvegltransform for ARM64
    GstElement *nvosd_display = nullptr;
    GstElement *sink_display = nullptr;
    GstElement *queue_file = nullptr;
    GstElement *sink_file = nullptr;
    GstElement *queue_mp4 = nullptr;
    GstElement *parser_mp4 = nullptr;
    GstElement *muxer_mp4 = nullptr;
    GstElement *sink_mp4 = nullptr;
    GstElement *queue_debug = nullptr;
    GstElement *parser_debug = nullptr;
    GstElement *decoder_debug = nullptr; ///< avdec_h265: software H.265 decoder (avoids CUDA conflicts)
    GstElement *converter_debug = nullptr; ///< videoconvert: pixel format conversion for pngenc
    GstElement *encoder_debug = nullptr;
    GstElement *sink_debug = nullptr;
    GstElement *queue_rtp = nullptr;
    GstElement *rtp_payloader = nullptr;
    GstElement *sink_rtp = nullptr;
    // WebRTC elements (used when transport_type=webrtc)
    GstElement *webrtcbin = nullptr;
};
