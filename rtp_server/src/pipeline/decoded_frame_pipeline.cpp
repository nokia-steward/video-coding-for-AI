// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "decoded_frame_pipeline.h"

#include "logging.h"
#include "pipeline.h"
#include "raw_video_caps_metadata.h"

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

void extract_video_properties(rtp_pipeline &pipeline, const sf::unique_gst_caps_no_delete_ptr &caps) {
    const GstStructure *structure = gst_caps_get_structure(caps.get(), 0);
    if (!structure) {
        return;
    }
    gint width = 0;
    gint height = 0;
    gint framerate_num = 0;
    gint framerate_den = 1;
    const gchar *format_str = gst_structure_get_string(structure, "format");

    if (!gst_structure_get_int(structure, "width", &width)) {
        sf::warning(pipeline_category, "Failed to get width from caps structure");
    }
    if (!gst_structure_get_int(structure, "height", &height)) {
        sf::warning(pipeline_category, "Failed to get height from caps structure");
    }
    if (!gst_structure_get_fraction(structure, "framerate", &framerate_num, &framerate_den)) {
        sf::warning(pipeline_category, "Failed to get framerate from caps structure");
    }

    auto &stats = pipeline.stats_context.statistics;
    stats.width = width;
    stats.height = height;
    // Only update framerate when caps provide a valid value (decoded caps often report 0/1; preserve prior rate).
    if (framerate_num > 0 && framerate_den > 0) {
        stats.framerate_numerator = framerate_num;
        stats.framerate_denominator = framerate_den;
    }
    stats.format = format_str;
}

void sync_nvstreammux_dimensions_if_needed(rtp_pipeline &pipeline) {
    if (!pipeline.server_config || !pipeline.server_config->inference_engine || !pipeline.nvstreammux) {
        return;
    }

    const gint w = pipeline.stats_context.statistics.width;
    const gint h = pipeline.stats_context.statistics.height;
    if (w <= 0 || h <= 0) {
        return;
    }

    gint streammux_width = 0;
    gint streammux_height = 0;
    g_object_get(G_OBJECT(pipeline.nvstreammux.get()), "width", &streammux_width, "height", &streammux_height, nullptr);
    if (streammux_width != w || streammux_height != h) {
        g_object_set(G_OBJECT(pipeline.nvstreammux.get()), "width", w, "height", h, nullptr);
        sf::info(pipeline_category,
                 "Updated nvstreammux resolution for port %u from %dx%d to %dx%d based on negotiated decoded caps",
                 pipeline.port, streammux_width, streammux_height, w, h);
    }
}

[[nodiscard]] sf::unique_gst_caps_ptr create_simplified_caps(const sf::unique_gst_caps_no_delete_ptr &original_caps) {
    if (!original_caps.get() || gst_caps_get_size(original_caps.get()) == 0) {
        return nullptr;
    }

    const GstStructure *structure = gst_caps_get_structure(original_caps.get(), 0);
    if (!structure) {
        return nullptr;
    }

    gint width = 0;
    gint height = 0;
    gint framerate_num = 0;
    gint framerate_den = 1;
    const gchar *format_str = gst_structure_get_string(structure, "format");

    if (!gst_structure_get_int(structure, "width", &width)) {
        sf::warning(pipeline_category, "Failed to get width from caps structure");
    }
    if (!gst_structure_get_int(structure, "height", &height)) {
        sf::warning(pipeline_category, "Failed to get height from caps structure");
    }
    if (!gst_structure_get_fraction(structure, "framerate", &framerate_num, &framerate_den)) {
        sf::warning(pipeline_category, "Failed to get framerate from caps structure");
    }

    const char *format_string = format_str ? format_str : sf::video_format_to_string(sf::video_format::i420);
    GstStructure *new_structure = gst_structure_new("video/x-raw", "format", G_TYPE_STRING, format_string, "width",
                                                    G_TYPE_INT, width, "height", G_TYPE_INT, height, "framerate",
                                                    GST_TYPE_FRACTION, framerate_num, framerate_den, nullptr);
    if (!new_structure) {
        return nullptr;
    }
    copy_preserved_raw_video_caps_fields(structure, new_structure);
    gst_structure_remove_field(new_structure, "memory");
    gst_structure_remove_field(new_structure, "nvbuf-memory-type");
    gst_structure_remove_field(new_structure, "block-linear");
    gst_structure_remove_field(new_structure, "gpu-id");

    sf::unique_gst_caps_ptr caps(gst_caps_new_empty());
    if (!caps) {
        gst_structure_free(new_structure);
        return nullptr;
    }

    gst_caps_append_structure(caps.get(), new_structure);

    if (!gst_caps_is_fixed(caps.get())) {
        sf::warning(pipeline_category, "Simplified caps are not fixed; downstream may reject them");
    }

    return caps;
}
