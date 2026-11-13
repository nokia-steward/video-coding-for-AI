// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

/**
 * @file encoder_enums.height
 * @brief Defines enums used throughout the encoder_cli application.
 */

/**
 * @enum input_type
 * @brief Defines input source types for the encoder.
 */
enum class input_type { file, camera, udp };

/**
 * @enum packer_type
 * @brief Defines ROI packing algorithm types.
 */
enum class packer_type { low_delay, random_access };

/**
 * @enum background_mode
 * @brief Defines background processing mode for ROI workflows.
 */
enum class background_mode {
    off, ///< Disable background-specific processing.
    background_blur, ///< Blur background pixels while preserving object ROIs.
    qp_map_delta ///< Use encoder-side QP delta map for background quantization.
};

/**
 * @enum roi_margin_mode
 * @brief Defines how fixed ROI margin is applied before CTU packing.
 */
enum class roi_margin_mode {
    off, ///< Disable fixed-pixel margin dilation.
    content, ///< Apply margin to copied/signaled ROI content and packing placement.
    placement ///< Apply margin only for packing placement while keeping original copied/signaled ROI content.
};

/**
 * @enum queue_leaky_mode
 * @brief Defines queue leaky modes for GStreamer queues.
 */
enum class queue_leaky_mode { no_leak = 0, leak_downstream = 2 };

/**
 * @enum roi_accumulation_mode
 * @brief Defines how ROIs are matched and accumulated across frames.
 */
enum class roi_accumulation_mode {
    object_id, ///< Use tracker's object_id for matching (requires tracker)
    overlap ///< Use IOU-based overlap matching (doesn't require tracker)
};

/**
 * @enum stabilization_mode
 * @brief Defines stabilization algorithm for ROI smoothing.
 */
enum class stabilization_mode { none, ema, threshold, hybrid };

/**
 * @brief Converts input_type enum to string.
 * @param type The input_type enum value.
 * @return String representation (e.g., "file", "camera", "udp").
 */
[[nodiscard]] const char *input_type_to_string(input_type type);

/**
 * @brief Converts string to input_type enum.
 * @param str The string representation.
 * @return The input_type enum value, or input_type::file if string is not recognized.
 */
[[nodiscard]] input_type string_to_input_type(const char *str);

/**
 * @brief Converts packer_type enum to string.
 * @param type The packer_type enum value.
 * @return String representation (e.g., "low_delay", "random_access").
 */
[[nodiscard]] const char *packer_type_to_string(packer_type type);

/**
 * @brief Converts string to packer_type enum.
 * @param str The string representation.
 * @return The packer_type enum value, or packer_type::low_delay if string is not recognized.
 */
[[nodiscard]] packer_type string_to_packer_type(const char *str);

/**
 * @brief Converts background_mode enum to string.
 * @param mode The background_mode enum value.
 * @return String representation (e.g., "off", "background_blur", "qp_map_delta").
 */
[[nodiscard]] const char *background_mode_to_string(background_mode mode);

/**
 * @brief Converts string to background_mode enum.
 * @param str The string representation.
 * @return The background_mode enum value, or background_mode::off if string is not recognized.
 */
[[nodiscard]] background_mode string_to_background_mode(const char *str);

/**
 * @brief Converts roi_margin_mode enum to string.
 * @param mode The roi_margin_mode enum value.
 * @return String representation (e.g., "off", "content", "placement").
 */
[[nodiscard]] const char *roi_margin_mode_to_string(roi_margin_mode mode);

/**
 * @brief Converts string to roi_margin_mode enum.
 * @param str The string representation.
 * @return The roi_margin_mode enum value, or roi_margin_mode::off if string is not recognized.
 */
[[nodiscard]] roi_margin_mode string_to_roi_margin_mode(const char *str);

/**
 * @brief Converts roi_accumulation_mode enum to string.
 * @param mode The roi_accumulation_mode enum value.
 * @return String representation (e.g., "object_id", "overlap").
 */
[[nodiscard]] const char *roi_accumulation_mode_to_string(roi_accumulation_mode mode);

/**
 * @brief Converts string to roi_accumulation_mode enum.
 * @param str The string representation.
 * @return The roi_accumulation_mode enum value, or roi_accumulation_mode::object_id if string is not recognized.
 */
[[nodiscard]] roi_accumulation_mode string_to_roi_accumulation_mode(const char *str);

/**
 * @brief Converts stabilization_mode enum to string.
 * @param mode The stabilization_mode enum value.
 * @return String representation (e.g., "none", "ema", "threshold", "hybrid").
 */
[[nodiscard]] const char *stabilization_mode_to_string(stabilization_mode mode);

/**
 * @brief Converts string to stabilization_mode enum.
 * @param str The string representation.
 * @return The stabilization_mode enum value, or stabilization_mode::hybrid if string is not recognized.
 */
[[nodiscard]] stabilization_mode string_to_stabilization_mode(const char *str);
