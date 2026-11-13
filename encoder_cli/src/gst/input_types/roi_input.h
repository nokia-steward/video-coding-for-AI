// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "bypass_path.h"

#include <gst/gst.h>

/**
 * @file roi_input.h
 * @brief ROI-related input pipeline creation and management.
 */

/**
 * @brief Forward declarations: aggregate only appears at the entry-creator boundary.
 */
struct global_data;
class input_ingress_facade;
struct encoder_config;

/**
 * @brief Initializes the ROI input debug category.
 */
void init_roi_input_debug_category();

/**
 * @brief Structure to hold DeepStream pipeline elements.
 */
struct deepstream_elements {
    GstElement *streammux = nullptr;
    GstElement *pgie = nullptr;
    GstElement *tracker = nullptr;
    GstElement *nvvidconv = nullptr;
    GstElement *nvvidconv2 = nullptr;
    GstElement *nvvidconv_pre_inference = nullptr; // Converter between appsrc and streammux
    GstElement *capsfilter_inference = nullptr; // Force system memory format for inference path
    GstElement *videoconvert_inference = nullptr; // Convert NVMM to system memory for inference appsink
    GstElement *appsink = nullptr;
    /// @brief Valve element for controlling inference path (for bypass feature)
    GstElement *valve_inference = nullptr;
    /// @brief Bypass path elements (created with the bypass valve closed).
    bypass_path_elements bypass{};

    // Default constructor explicitly initializes bypass
    deepstream_elements() = default;
};

/**
 * @brief Creates DeepStream elements for ROI detection.
 * @param elements Output structure to populate with created elements.
 * @param config Encoder configuration used to decide which elements are created.
 * @return true if all elements were created successfully, false otherwise.
 */
[[nodiscard]] bool create_deepstream_elements(deepstream_elements &elements, const encoder_config &config);

/**
 * @brief Configures DeepStream elements from configuration file.
 * @param data The narrow input/ingress view.
 * @param elements The DeepStream elements to configure (non-const to allow configuration).
 * @return true if configuration was successful, false otherwise.
 */
[[nodiscard]] bool configure_deepstream_elements(input_ingress_facade &data, deepstream_elements &elements);

/**
 * @brief Generates a tracker configuration file with custom parameters.
 * @param base_config_path Path to the base tracker config file.
 * @param output_path Path where the generated config file should be written.
 * @param max_shadow_tracking_age Maximum shadow tracking age to set.
 * @param enable_reassoc Whether to enable re-association.
 * @param trajectory_projection_length Trajectory projection length.
 * @param max_tracklet_matching_time_range Maximum tracklet matching time range.
 * @return true if generation was successful, false otherwise.
 */
[[nodiscard]] bool generate_tracker_config_file(const std::string &base_config_path, const std::string &output_path,
                                                int max_shadow_tracking_age, bool enable_reassoc,
                                                int trajectory_projection_length, int max_tracklet_matching_time_range,
                                                int max_targets_per_stream, int probation_age,
                                                int early_termination_age, double min_confidence,
                                                bool use_prediction_for_assoc);

/**
 * @brief Configures the nvtracker element.
 * @param config Encoder configuration.
 * @param tracker The tracker element to configure.
 * @return true if configuration was successful, false otherwise.
 */
[[nodiscard]] bool configure_tracker(const encoder_config &config, GstElement *tracker);

/**
 * @brief Configures the nvvideoconvert elements to use GPU instead of VIC.
 * @param elements The DeepStream elements containing nvvideoconvert elements.
 * @param data Narrow input/ingress view (used to store memory-contract expectation).
 */
void configure_nvvideoconvert_elements(const deepstream_elements &elements, input_ingress_facade &data);

/**
 * @brief Sets up pad probes to preserve PTS through the inference path.
 * Streammux resets PTS, so we store original PTS before streammux and restore it after.
 * Also sets up performance monitoring probes if enabled.
 * @param data Narrow input/ingress view (for ingress runtime + monitoring + memory policy).
 * @param elements The DeepStream elements.
 * @return true if probes were set up successfully.
 */
[[nodiscard]] bool setup_pts_preservation_probes(input_ingress_facade &data, deepstream_elements &elements);

/**
 * @brief Links DeepStream elements together.
 * @param data Narrow input/ingress view.
 * @param elements The DeepStream elements to link (non-const to allow linking).
 * @param tee The tee element to link bypass path to (nullptr if bypass disabled).
 * @return true if linking was successful, false otherwise.
 */
[[nodiscard]] bool link_deepstream_elements(input_ingress_facade &data, deepstream_elements &elements,
                                            GstElement *tee = nullptr);

/**
 * @brief Adds DeepStream elements to the pipeline.
 * @param data Narrow input/ingress view.
 * @param elements The DeepStream elements (non-const to allow adding to pipeline).
 * @param queue_original Original frame saving queue element.
 * @param converter_original Original frame saving converter element.
 * @param encoder_original Original frame saving encoder element.
 * @param sink_original Original frame saving sink element.
 */
void add_deepstream_elements_to_pipeline(const input_ingress_facade &data, deepstream_elements &elements,
                                         GstElement *queue_original, GstElement *converter_original,
                                         GstElement *encoder_original, GstElement *sink_original);

/**
 * @brief Creates an ROI info input pipeline.
 * @param data The global data structure.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_roi_info_input_pipeline(global_data &data);

/**
 * @brief Creates an ROI detection input pipeline with DeepStream.
 * @param data The global data structure.
 * @return true on success, false on failure.
 */
[[nodiscard]] bool create_roi_detection_input_pipeline(global_data &data);

/**
 * @brief Gets the current size of the PTS storage queue for diagnostics.
 * @param data Narrow input/ingress view that owns ingress runtime state.
 * @return Current queue size, or 0 if PTS storage is not initialized.
 */
[[nodiscard]] uint64_t get_pts_storage_queue_size(const input_ingress_facade &data);
