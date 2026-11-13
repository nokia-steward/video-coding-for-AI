// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file encoded_stream_ports.h
 * @brief Non-owning handles for encoder egress appsrc elements (encoding + SEI).
 *
 * Centralizes access so bridge, backend, and pipeline code share one choke point;
 * a future custom element can replace underlying GstElement types without fan-out
 * refactors across the tree.
 */
struct encoded_stream_ports {
    /**
     * @brief Non-owning pointer to the encoding appsrc element.
     */
    GstElement *encoding_src = nullptr;

    /**
     * @brief Non-owning pointer to the SEI appsrc element.
     */
    GstElement *sei_src = nullptr;
};
