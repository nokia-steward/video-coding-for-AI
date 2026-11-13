// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_enums.h"

#include <cstring>

/**
 * @file encoder_enums.cpp
 * @brief Implementation of enum conversion functions for encoder_cli.
 */

/**
 * @brief Converts input_type enum to string.
 * @param type The input_type enum value.
 * @return String representation (e.g., "file", "camera", "udp").
 */
const char *input_type_to_string(input_type type) {
    switch (type) {
        case input_type::file:
            return "file";
        case input_type::camera:
            return "camera";
        case input_type::udp:
            return "udp";
        default:
            return "file"; // Default fallback
    }
}

/**
 * @brief Converts string to input_type enum.
 * @param str The string representation.
 * @return The input_type enum value, or input_type::file if string is not recognized.
 */
input_type string_to_input_type(const char *str) {
    if (std::strcmp(str, "file") == 0) {
        return input_type::file;
    }
    if (std::strcmp(str, "camera") == 0) {
        return input_type::camera;
    }
    if (std::strcmp(str, "udp") == 0) {
        return input_type::udp;
    }
    return input_type::file; // Default fallback
}

/**
 * @brief Converts packer_type enum to string.
 * @param type The packer_type enum value.
 * @return String representation (e.g., "low_delay", "random_access").
 */
const char *packer_type_to_string(packer_type type) {
    switch (type) {
        case packer_type::low_delay:
            return "low_delay";
        case packer_type::random_access:
            return "random_access";
        default:
            return "low_delay"; // Default fallback
    }
}

/**
 * @brief Converts string to packer_type enum.
 * @param str The string representation.
 * @return The packer_type enum value, or packer_type::low_delay if string is not recognized.
 */
packer_type string_to_packer_type(const char *str) {
    if (std::strcmp(str, "low_delay") == 0) {
        return packer_type::low_delay;
    }
    if (std::strcmp(str, "random_access") == 0) {
        return packer_type::random_access;
    }
    return packer_type::low_delay; // Default fallback
}

/**
 * @brief Converts background_mode enum to string.
 * @param mode The background_mode enum value.
 * @return String representation (e.g., "off", "background_blur", "qp_map_delta").
 */
const char *background_mode_to_string(const background_mode mode) {
    switch (mode) {
        case background_mode::off:
            return "off";
        case background_mode::background_blur:
            return "background_blur";
        case background_mode::qp_map_delta:
            return "qp_map_delta";
        default:
            return "off"; // Default fallback
    }
}

/**
 * @brief Converts string to background_mode enum.
 * @param str The string representation.
 * @return The background_mode enum value, or background_mode::off if string is not recognized.
 */
background_mode string_to_background_mode(const char *str) {
    if (std::strcmp(str, "off") == 0) {
        return background_mode::off;
    }
    if (std::strcmp(str, "background_blur") == 0) {
        return background_mode::background_blur;
    }
    if (std::strcmp(str, "qp_map_delta") == 0) {
        return background_mode::qp_map_delta;
    }
    return background_mode::off; // Default fallback
}

/**
 * @brief Converts roi_margin_mode enum to string.
 * @param mode The roi_margin_mode enum value.
 * @return String representation (e.g., "off", "content", "placement").
 */
const char *roi_margin_mode_to_string(const roi_margin_mode mode) {
    switch (mode) {
        case roi_margin_mode::off:
            return "off";
        case roi_margin_mode::content:
            return "content";
        case roi_margin_mode::placement:
            return "placement";
        default:
            return "off"; // Default fallback
    }
}

/**
 * @brief Converts string to roi_margin_mode enum.
 * @param str The string representation.
 * @return The roi_margin_mode enum value, or roi_margin_mode::off if string is not recognized.
 */
roi_margin_mode string_to_roi_margin_mode(const char *str) {
    if (std::strcmp(str, "off") == 0) {
        return roi_margin_mode::off;
    }
    if (std::strcmp(str, "content") == 0) {
        return roi_margin_mode::content;
    }
    if (std::strcmp(str, "placement") == 0) {
        return roi_margin_mode::placement;
    }
    return roi_margin_mode::off; // Default fallback
}

/**
 * @brief Converts roi_accumulation_mode enum to string.
 * @param mode The roi_accumulation_mode enum value.
 * @return String representation (e.g., "object_id", "overlap").
 */
const char *roi_accumulation_mode_to_string(roi_accumulation_mode mode) {
    switch (mode) {
        case roi_accumulation_mode::object_id:
            return "object_id";
        case roi_accumulation_mode::overlap:
            return "overlap";
        default:
            return "object_id"; // Default fallback
    }
}

/**
 * @brief Converts string to roi_accumulation_mode enum.
 * @param str The string representation.
 * @return The roi_accumulation_mode enum value, or roi_accumulation_mode::object_id if string is not recognized.
 */
roi_accumulation_mode string_to_roi_accumulation_mode(const char *str) {
    if (std::strcmp(str, "object_id") == 0) {
        return roi_accumulation_mode::object_id;
    }
    if (std::strcmp(str, "overlap") == 0) {
        return roi_accumulation_mode::overlap;
    }
    return roi_accumulation_mode::object_id; // Default fallback
}

/**
 * @brief Converts stabilization_mode enum to string.
 * @param mode The stabilization_mode enum value.
 * @return String representation (e.g., "none", "ema", "threshold", "hybrid").
 */
const char *stabilization_mode_to_string(const stabilization_mode mode) {
    switch (mode) {
        case stabilization_mode::none:
            return "none";
        case stabilization_mode::ema:
            return "ema";
        case stabilization_mode::threshold:
            return "threshold";
        case stabilization_mode::hybrid:
            return "hybrid";
        default:
            return "hybrid"; // Default fallback
    }
}

/**
 * @brief Converts string to stabilization_mode enum.
 * @param str The string representation.
 * @return The stabilization_mode enum value, or stabilization_mode::hybrid if string is not recognized.
 */
stabilization_mode string_to_stabilization_mode(const char *str) {
    if (std::strcmp(str, "none") == 0) {
        return stabilization_mode::none;
    }
    if (std::strcmp(str, "ema") == 0) {
        return stabilization_mode::ema;
    }
    if (std::strcmp(str, "threshold") == 0) {
        return stabilization_mode::threshold;
    }
    if (std::strcmp(str, "hybrid") == 0) {
        return stabilization_mode::hybrid;
    }
    return stabilization_mode::hybrid; // Default fallback
}
