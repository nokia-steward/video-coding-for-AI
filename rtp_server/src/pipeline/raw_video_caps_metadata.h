// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <array>

#include <gst/gst.h>

/**
 * @brief Copies selected raw-video metadata fields that affect downstream
 *        interpretation of decoded frames.
 * @param source Source caps structure.
 * @param destination Destination caps structure.
 */
inline void copy_preserved_raw_video_caps_fields(const GstStructure *source, GstStructure *destination) {
    if (!source || !destination) {
        return;
    }

    constexpr std::array<const char *, 9> preserved_field_names = {
            "pixel-aspect-ratio", "interlace-mode", "field-order",     "chroma-site", "colorimetry",
            "color-range",        "multiview-mode", "multiview-flags", "views",
    };

    for (const char *field_name: preserved_field_names) {
        if (const GValue *field_value = gst_structure_get_value(source, field_name); field_value != nullptr) {
            gst_structure_set_value(destination, field_name, field_value);
        }
    }
}
