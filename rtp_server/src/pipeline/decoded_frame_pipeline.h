// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include <gst/gst.h>

#include "gst_raii.h"

struct rtp_pipeline;

/**
 * @brief Extracts video properties from caps and stores them in pipeline statistics.
 */
void extract_video_properties(rtp_pipeline &pipeline, const sf::unique_gst_caps_no_delete_ptr &caps);

/**
 * @brief When inference is enabled, align nvstreammux width/height with negotiated frame size.
 */
void sync_nvstreammux_dimensions_if_needed(rtp_pipeline &pipeline);

/**
 * @brief Creates fixed output caps using an explicit system-memory contract
 *        while preserving selected negotiated raw-video metadata needed by downstream elements.
 * @param original_caps The original caps to simplify (non-owning reference).
 * @return Simplified caps.
 */
[[nodiscard]] sf::unique_gst_caps_ptr create_simplified_caps(const sf::unique_gst_caps_no_delete_ptr &original_caps);
